#ifndef REGISTRY_H
#define REGISTRY_H

#include "expr/fwd.h"
#include "expr/op.h"
#include "expr/value.h"

#include <stddef.h>

// Everything a plugin can add. Each entry remembers which plugin added it,
// and duplicates are rejected.

/** One registered descriptor and the plugin that added it. */
typedef struct {
    const void *def;
    const char *owner;
} reg_entry_t;

/** Growable list of entries. */
typedef struct {
    reg_entry_t *data;
    size_t len;
    size_t cap;
} reg_vec_t;

/** All registered descriptors, by kind. */
typedef struct {
    reg_vec_t types;
    reg_vec_t fns;
    reg_vec_t ops;
    reg_vec_t literals;
    reg_vec_t constants;
    reg_vec_t passes;
} registry_t;

/** Sets up an empty registry. */
void registry_init(registry_t *r);

/** Frees the registry itself. The descriptors belong to the plugins. */
void registry_destroy(registry_t *r);

/** Registers a value type. Fails on a duplicate name. */
cas_status_t registry_add_type(cas_t *cas, const value_type_t *t);

/** Registers a function. Fails on a duplicate name or if min_args > max_args. */
cas_status_t registry_add_fn(cas_t *cas, const fn_def_t *fn);

/**
 * Registers operator syntax. The symbol must be all letters or all punctuation,
 * and the function must accept 2 arguments (infix) or 1 (prefix, postfix).
 */
cas_status_t registry_add_op(cas_t *cas, const op_def_t *op);

/** Registers a number syntax. Fails on a duplicate name. */
cas_status_t registry_add_literal(cas_t *cas, const literal_def_t *lit);

/** Registers a named constant. Fails on a duplicate name. */
cas_status_t registry_add_constant(cas_t *cas, const constant_def_t *c);

/** Appends a pass to the pipeline. Passes run in registration order. */
cas_status_t registry_add_pass(cas_t *cas, const pass_def_t *p);

/** Looks up a value type by name. NULL if unknown. */
const value_type_t *registry_find_type(const cas_t *cas, const char *name);

/** Looks up a function by name (not NUL-terminated, len bytes). NULL if unknown. */
const fn_def_t *registry_find_fn(const cas_t *cas, const char *name, size_t len);

/** Looks up a constant by name (len bytes). NULL if unknown. */
const constant_def_t *registry_find_constant(const cas_t *cas, const char *name, size_t len);

/** Looks up an operator by symbol (len bytes) and fixity. NULL if unknown. */
const op_def_t *registry_find_op(const cas_t *cas, const char *symbol, size_t len, fixity_t fixity);

/** Finds the operator that gives fn its syntax, for printing. NULL if there is none. */
const op_def_t *registry_op_for_fn(const cas_t *cas, const fn_def_t *fn, fixity_t fixity);

/**
 * Longest symbolic operator at the start of s, or NULL. Word operators like
 * "mod" are matched as identifiers instead.
 */
const op_def_t *registry_match_op(const cas_t *cas, const char *s, size_t len);

/** True if name (len bytes) is the symbol of any registered operator. */
int registry_is_op_symbol(const cas_t *cas, const char *name, size_t len);

/** Longest literal at the start of s. *out_len gets its length, 0 if none. */
const literal_def_t *registry_match_literal(const cas_t *cas, const char *s, size_t len, size_t *out_len);

/** Left binding power of op. */
static inline int op_lbp(const op_def_t *op)
{
    return op->precedence * 2 + (op->fixity == FIX_INFIX && op->assoc == ASSOC_RIGHT);
}

/** Right binding power of op. */
static inline int op_rbp(const op_def_t *op)
{
    return op->precedence * 2 + (op->fixity == FIX_INFIX && op->assoc == ASSOC_LEFT);
}

#endif // REGISTRY_H
