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
    size_t history_entries;  // lines the history keeps before the oldest is dropped (default 1000)
    size_t history_line_max; // bytes per history line, terminator included (default 512)
    size_t simplify_max_rounds; // rounds of the pass pipeline before cas_simplify gives up on a stable form (default 8)
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
    arena_t history_arena;
    arena_ring_t history; // fixed-size NUL-terminated lines, newest last
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

/**
 * Remembers an input line for the REPL's recall. Returns -1 and stores nothing
 * if the line is empty or does not fit a history slot (history_line_max), since
 * a truncated entry would recall as a different line. Repeating the newest
 * entry is accepted but not stored twice. The oldest entry is dropped when full.
 */
int cas_history_add(cas_t *cas, const char *line, size_t len);

/** Number of lines in the history. */
size_t cas_history_count(const cas_t *cas);

/** The line `back` entries before the newest (0 = newest), or NULL if out of range. Valid until the next cas_history_add. */
const char *cas_history_get(const cas_t *cas, size_t back);

/** Allocates from the scratch arena. Reports an error and returns NULL when it is full. */
void *cas_scratch_alloc(cas_t *cas, size_t size);

/**
 * Calls fn(ptr) at the next scratch reset. Values that own heap memory
 * (rationals, floats) use this so the arena can be dropped in one go.
 */
cas_status_t cas_defer(cas_t *cas, void (*fn)(void *), void *ptr);

/** Runs the deferred cleanups and frees everything allocated since the last reset, diagnostics included. */
void cas_scratch_reset(cas_t *cas);

/**
 * Runs all registered passes over root, repeating the whole pipeline until it
 * changes nothing or simplify_max_rounds is used up. Returns NULL on failure.
 */
expr_t *cas_simplify(cas_t *cas, expr_t *root);

/**
 * Runs the display passes over a simplified result. The result of cas_eval
 * stays canonical (that is what history and later passes should see); only
 * the returned tree is meant for expr_to_str. Returns NULL on failure.
 */
expr_t *cas_present(cas_t *cas, expr_t *root);

#endif // CAS_H
