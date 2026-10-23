#include "engine/cas.h"
#include "engine/plugin.h"
#include "eval/simplify.h"
#include "input/parser.h"
#include "misc/arena.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FLOAT_BITS 128
#define DEFAULT_SCRATCH_RESERVE ((size_t)256 << 20)
#define DEFAULT_HISTORY_ENTRIES 1000
#define DEFAULT_HISTORY_LINE_MAX 512

struct cas_defer_node {
    void (*fn)(void *);
    void *ptr;
    cas_defer_node_t *next;
};

/** Reserves the history arena and builds the ring in it. Returns -1 on failure or if the size overflows. */
static int history_init(cas_t *cas)
{
    size_t entries = cas->opts.history_entries;
    size_t line_max = cas->opts.history_line_max;

    if (entries > SIZE_MAX / line_max)
        return -1;
    if (arena_init(&cas->history_arena, entries * line_max) != 0)
        return -1;
    return arena_ring_init(&cas->history, &cas->history_arena, line_max, entries);
}

/** Allocates a session and sets up the scratch and history arenas and the built-in passes. */
cas_t *cas_new(const cas_options_t *opts)
{
    cas_t *cas = calloc(1, sizeof(*cas));
    if (!cas)
        return NULL;

    if (opts)
        cas->opts = *opts;
    if (!cas->opts.float_bits)
        cas->opts.float_bits = DEFAULT_FLOAT_BITS;
    if (!cas->opts.scratch_reserve)
        cas->opts.scratch_reserve = DEFAULT_SCRATCH_RESERVE;
    if (!cas->opts.history_entries)
        cas->opts.history_entries = DEFAULT_HISTORY_ENTRIES;
    if (!cas->opts.history_line_max)
        cas->opts.history_line_max = DEFAULT_HISTORY_LINE_MAX;

    registry_init(&cas->reg);
    if (arena_init(&cas->scratch, cas->opts.scratch_reserve) != 0 ||
        history_init(cas) != 0 ||
        registry_add_pass(cas, &pass_fold_constants) != CAS_OK ||
        registry_add_pass(cas, &pass_rewrite_expr) != CAS_OK ||
        registry_add_pass(cas, &pass_collect_assoc) != CAS_OK) {
        registry_destroy(&cas->reg);
        arena_destroy(&cas->scratch);
        arena_destroy(&cas->history_arena);
        free(cas);
        return NULL;
    }
    return cas;
}

/** Tears a session down: scratch values first, then plugin shutdown, registry, and dlclose last. */
void cas_free(cas_t *cas)
{
    if (!cas)
        return;
    cas_scratch_reset(cas);

    for (size_t i = cas->nplugins; i-- > 0;) {
        const plugin_desc_t *d = cas->plugins[i].desc;
        if (d->shutdown)
            d->shutdown(cas);
    }
    registry_destroy(&cas->reg);
    // The registry points into the shared objects, so close them after it is gone.
    for (size_t i = cas->nplugins; i-- > 0;) {
        if (cas->plugins[i].handle)
            dlclose(cas->plugins[i].handle);
    }
    free(cas->plugins);
    arena_destroy(&cas->scratch);
    arena_destroy(&cas->history_arena);
    free(cas);
}

/** Copies the line into the next ring slot, dropping the oldest entry if full. */
int cas_history_add(cas_t *cas, const char *line, size_t len)
{
    arena_ring_t *h = &cas->history;

    if (len == 0 || len >= h->elem_size)
        return -1;

    if (h->len > 0) {
        const char *newest = arena_ring_get(h, h->len - 1);
        if (strncmp(newest, line, len) == 0 && newest[len] == '\0')
            return 0;
    }

    char *slot = arena_ring_push_overwrite(h, NULL);
    if (!slot)
        return -1;
    memcpy(slot, line, len);
    slot[len] = '\0';
    return 0;
}

/** Entries currently stored. */
size_t cas_history_count(const cas_t *cas)
{
    return cas->history.len;
}

/** Counts back from the newest entry. */
const char *cas_history_get(const cas_t *cas, size_t back)
{
    if (back >= cas->history.len)
        return NULL;
    return arena_ring_get(&cas->history, cas->history.len - 1 - back);
}

/** Bump-allocates from the scratch arena. */
void *cas_scratch_alloc(cas_t *cas, size_t size)
{
    void *p = arena_alloc(&cas->scratch, size);
    if (!p)
        cas_error(cas, CAS_ERR_NOMEM, "out of memory");
    return p;
}

/** Queues a cleanup for the next scratch reset. */
cas_status_t cas_defer(cas_t *cas, void (*fn)(void *), void *ptr)
{
    cas_defer_node_t *n = cas_scratch_alloc(cas, sizeof(*n));
    if (!n)
        return CAS_ERR_NOMEM;
    n->fn = fn;
    n->ptr = ptr;
    n->next = cas->defers;
    cas->defers = n;
    return CAS_OK;
}

/** Runs queued cleanups, clears diagnostics, empties the arena. */
void cas_scratch_reset(cas_t *cas)
{
    for (cas_defer_node_t *n = cas->defers; n; n = n->next)
        n->fn(n->ptr);
    cas->defers = NULL;
    cas->diags.len = 0;
    arena_reset(&cas->scratch);
}

/** Parses src, then runs the passes over the tree. */
cas_status_t cas_eval(cas_t *cas, const char *src, size_t len, cas_result_t *out)
{
    out->tree = out->result = NULL;
    cas->diags.len = 0;
    CAS_LOG(cas, LOG_INFO, "eval '%.*s'", (int)len, src);

    cas_status_t st = parser_parse(cas, src, len, &out->tree);
    if (st != CAS_OK)
        return st;
    CAS_LOG(cas, LOG_DEBUG, "parsed");

    out->result = cas_simplify(cas, out->tree);
    if (!out->result) {
        // Passes report their own errors, but make sure a failure always says something.
        if (cas->diags.len == 0)
            cas_error(cas, CAS_ERR_INTERNAL, "simplification failed");
        return cas->diags.items[cas->diags.len - 1].status;
    }
    return CAS_OK;
}
