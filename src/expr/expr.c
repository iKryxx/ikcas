#include "expr/expr.h"
#include "engine/cas.h"

#include <string.h>

/** Allocates a zeroed node. */
static expr_t *node_new(cas_t *cas, expr_kind_t kind, span_t span)
{
    expr_t *e = cas_scratch_alloc(cas, sizeof(*e));
    if (!e)
        return NULL;
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->span = span;
    return e;
}

/** Deferred cleanup for a constant node. */
static void destroy_node_value(void *p)
{
    value_destroy(&((expr_t *)p)->as.value);
}

/** Makes a constant node and takes ownership of v. */
expr_t *expr_new_const(cas_t *cas, span_t span, value_t v)
{
    expr_t *e = node_new(cas, EXPR_CONST, span);
    if (!e || cas_defer(cas, destroy_node_value, e) != CAS_OK) {
        value_destroy(&v);
        return NULL;
    }
    e->as.value = v;
    return e;
}

/** Makes a symbol node with a private copy of the name. */
expr_t *expr_new_symbol(cas_t *cas, span_t span, const char *name, size_t len)
{
    expr_t *e = node_new(cas, EXPR_SYMBOL, span);
    char *copy = cas_scratch_alloc(cas, len + 1);
    if (!e || !copy)
        return NULL;
    memcpy(copy, name, len);
    copy[len] = '\0';
    e->as.sym.name = copy;
    e->as.sym.len = len;
    return e;
}

/** Makes an application node with its own copy of the args array. */
expr_t *expr_new_apply(cas_t *cas, span_t span, const fn_def_t *fn, expr_t *const *args, size_t nargs)
{
    expr_t *e = node_new(cas, EXPR_APPLY, span);
    expr_t **copy = cas_scratch_alloc(cas, (nargs ? nargs : 1) * sizeof(*copy));
    if (!e || !copy)
        return NULL;
    memcpy(copy, args, nargs * sizeof(*copy));
    e->as.apply.fn = fn;
    e->as.apply.args = copy;
    e->as.apply.nargs = nargs;
    return e;
}

/** True for constant nodes. */
int expr_is_const(const expr_t *e)
{
    return e->kind == EXPR_CONST;
}

/** True for applications of fn. */
int expr_is_apply_of(const expr_t *e, const fn_def_t *fn)
{
    return e->kind == EXPR_APPLY && e->as.apply.fn == fn;
}

/** Copies a constant node, including its value. */
static expr_t *clone_const(cas_t *cas, const expr_t *e)
{
    value_t v;
    if (value_copy(cas, &v, &e->as.value) != CAS_OK)
        return NULL;
    return expr_new_const(cas, e->span, v);
}

/** Deep-copies a tree. */
expr_t *expr_clone(cas_t *cas, const expr_t *e)
{
    switch (e->kind) {
    case EXPR_CONST:
        return clone_const(cas, e);
    case EXPR_SYMBOL:
        return expr_new_symbol(cas, e->span, e->as.sym.name, e->as.sym.len);
    case EXPR_APPLY: {
        size_t n = e->as.apply.nargs;
        expr_t **args = cas_scratch_alloc(cas, (n ? n : 1) * sizeof(*args));
        if (!args)
            return NULL;
        for (size_t i = 0; i < n; i++) {
            if (!(args[i] = expr_clone(cas, e->as.apply.args[i])))
                return NULL;
        }
        return expr_new_apply(cas, e->span, e->as.apply.fn, args, n);
    }
    }
    return NULL;
}

/** Structural equality. */
int expr_equal(const expr_t *a, const expr_t *b)
{
    if (a->kind != b->kind)
        return 0;
    switch (a->kind) {
    case EXPR_CONST:
        return value_equal(&a->as.value, &b->as.value);
    case EXPR_SYMBOL:
        return a->as.sym.len == b->as.sym.len && memcmp(a->as.sym.name, b->as.sym.name, a->as.sym.len) == 0;
    case EXPR_APPLY:
        if (a->as.apply.fn != b->as.apply.fn || a->as.apply.nargs != b->as.apply.nargs)
            return 0;
        for (size_t i = 0; i < a->as.apply.nargs; i++) {
            if (!expr_equal(a->as.apply.args[i], b->as.apply.args[i]))
                return 0;
        }
        return 1;
    }
    return 0;
}

/** Bottom-up rewrite. Unchanged subtrees are shared with the input. */
expr_t *expr_map(cas_t *cas, expr_t *e, expr_map_fn_t fn, void *user)
{
    if (e->kind != EXPR_APPLY)
        return fn(cas, e, user);

    size_t n = e->as.apply.nargs;
    expr_t **args = cas_scratch_alloc(cas, (n ? n : 1) * sizeof(*args));
    if (!args)
        return NULL;

    int changed = 0;
    for (size_t i = 0; i < n; i++) {
        args[i] = expr_map(cas, e->as.apply.args[i], fn, user);
        if (!args[i])
            return NULL;
        changed |= args[i] != e->as.apply.args[i];
    }

    expr_t *node = e;
    if (changed && !(node = expr_new_apply(cas, e->span, e->as.apply.fn, args, n)))
        return NULL;
    return fn(cas, node, user);
}
