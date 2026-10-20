#ifndef CAS_H
#define CAS_H

#include "engine/diag.h"
#include "engine/registry.h"
#include "expr/expr.h"
#include "expr/fwd.h"
#include "misc/arena.h"

#include <stddef.h>

/** Session settings. A zero field means the default. */
typedef struct {
    size_t float_bits;      // float precision in bits (default 128)
    size_t scratch_reserve; // virtual bytes reserved for the scratch arena (default 256 MB)
    int verbosity;          // highest log_level_t to emit, 0 = quiet
} cas_options_t;

/** A loaded plugin. */
typedef struct {
    const void *desc; // const plugin_desc_t *
    void *handle;     // dlopen handle, NULL if linked into the binary
} cas_plugin_slot_t;

/** One pending cleanup, see cas_defer. */

typedef struct cas_defer_node cas_defer_node_t;

/**
 * One interpreter session: the registry (what the language knows) and the
 * scratch memory for the current line (what the input produced).
 */
struct cas {
    cas_options_t opts;
    arena_t scratch;
    registry_t reg;
    diag_list_t diags;
    cas_log_sink_t log_sink; // NULL = stderr
    void *log_user;
    span_t cur_span;
    const char *current_plugin; // recorded as owner of new registry entries
    cas_defer_node_t *defers;
    cas_plugin_slot_t *plugins;
    size_t nplugins;
    size_t plugins_cap;
};

/** What cas_eval produces for one line. */
typedef struct {
    expr_t *tree;   // as parsed
    expr_t *result; // after the simplification passes
} cas_result_t;

/** Creates a session. opts may be NULL for the defaults. Returns NULL on failure. */
cas_t *cas_new(const cas_options_t *opts);

/** Shuts down plugins and frees the session. Accepts NULL. */
void cas_free(cas_t *cas);

/**
 * Parses and simplifies one line. Errors end up in cas->diags. The returned
 * expressions stay valid until the next cas_scratch_reset.
 */
cas_status_t cas_eval(cas_t *cas, const char *src, size_t len, cas_result_t *out);

/** Allocates from the scratch arena. Reports an error and returns NULL when it is full. */
void *cas_scratch_alloc(cas_t *cas, size_t size);

/**
 * Calls fn(ptr) at the next scratch reset. Values that own heap memory
 * (rationals, floats) use this so the arena can be dropped in one go.
 */
cas_status_t cas_defer(cas_t *cas, void (*fn)(void *), void *ptr);

/** Runs the deferred cleanups and frees everything allocated since the last reset, diagnostics included. */
void cas_scratch_reset(cas_t *cas);

/** Runs all registered passes over root. Returns NULL on failure. */
expr_t *cas_simplify(cas_t *cas, expr_t *root);

#endif // CAS_H
