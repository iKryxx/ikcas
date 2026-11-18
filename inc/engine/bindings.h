#ifndef BINDINGS_H
#define BINDINGS_H

#include "expr/fwd.h"

#include <stddef.h>

// Names with a value that outlives one line, like `ans` (and later `a = 2`).
// Unlike the registry, which holds static definitions from plugins, bindings
// own their memory: each value is a heap tree made by expr_persist, so it
// survives cas_scratch_reset.

/** Flags of a binding. */
enum {
    BIND_READONLY = 1u << 0, // only the engine may set it, never a user assignment
};

/** One named value. */
typedef struct {
    char *name;     // malloc'd, NUL-terminated
    size_t len;
    expr_t *value;  // heap tree from expr_persist, NULL while declared but unset
    unsigned flags;
} binding_t;

/** All bindings of a session. A zeroed table is empty and valid. */
typedef struct {
    binding_t *items;
    size_t len;
    size_t cap;
} binding_table_t;

/**
 * Reserves a name without a value, e.g. binding_declare(cas, "ans", BIND_READONLY)
 * at startup, so the name is known before anything was evaluated.
 */
cas_status_t binding_declare(cas_t *cas, const char *name, unsigned flags);

/** The binding called name (len bytes, not terminated), or NULL. */
const binding_t *binding_find(const cas_t *cas, const char *name, size_t len);

/**
 * Stores a heap copy of value under name, creating the binding if needed.
 * This is the engine's setter and ignores BIND_READONLY; user assignment
 * must check the flag itself. On failure the old value is kept.
 * Example: binding_set(cas, "ans", 3, res.result).
 */
cas_status_t binding_set(cas_t *cas, const char *name, size_t len, const expr_t *value);

/** Frees every binding and its value. */
void binding_table_destroy(binding_table_t *t);

#endif // BINDINGS_H
