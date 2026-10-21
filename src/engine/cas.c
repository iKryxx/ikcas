#include "engine/cas.h"
#include "engine/plugin.h"
#include "eval/simplify.h"
#include "input/parser.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FLOAT_BITS 128
#define DEFAULT_SCRATCH_RESERVE ((size_t)256 << 20)

struct cas_defer_node {
    void (*fn)(void *);
    void *ptr;
    cas_defer_node_t *next;
};

/** Allocates a session and sets up the scratch arena and the built-in fold pass. */
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

    registry_init(&cas->reg);
    if (arena_init(&cas->scratch, cas->opts.scratch_reserve) != 0 ||
        registry_add_pass(cas, &pass_fold_constants) != CAS_OK ||
        registry_add_pass(cas, &pass_collect_assoc) != CAS_OK) {
        registry_destroy(&cas->reg);
        arena_destroy(&cas->scratch);
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
    free(cas);
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
