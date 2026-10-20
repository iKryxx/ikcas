#ifndef EXPR_H
#define EXPR_H

#include "expr/fwd.h"
#include "expr/op.h"
#include "expr/value.h"
#include "misc/span.h"

#include <stddef.h>

typedef enum {
    EXPR_CONST,
    EXPR_SYMBOL,
    EXPR_APPLY, // every operator and function call
} expr_kind_t;

/**
 * A node of an expression tree. Nodes live in the session's scratch arena
 * and never change after they're built. Passes make new nodes instead, so
 * the parse tree is still around after simplification.
 */
struct expr {
    expr_kind_t kind;
    span_t span;
    union {
        value_t value;
        struct {
            const char *name; // NUL-terminated
            size_t len;
        } sym;
        struct {
            const fn_def_t *fn;
            expr_t **args;
            size_t nargs;
        } apply;
    } as;
};

/**
 * Makes a constant node. It takes ownership of v and destroys it when the
 * scratch arena is reset. If allocation fails, v is destroyed right away and
 * the result is NULL.
 */
expr_t *expr_new_const(cas_t *cas, span_t span, value_t v);

/** Makes a symbol node. The name is copied. */
expr_t *expr_new_symbol(cas_t *cas, span_t span, const char *name, size_t len);

/** Makes an application node. The args array is copied, the nodes in it are shared. */
expr_t *expr_new_apply(cas_t *cas, span_t span, const fn_def_t *fn, expr_t *const *args, size_t nargs);

/** True if e is a constant. */
int expr_is_const(const expr_t *e);

/** True if e is an application of fn. */
int expr_is_apply_of(const expr_t *e, const fn_def_t *fn);

/** Deep copy of e. */
expr_t *expr_clone(cas_t *cas, const expr_t *e);

/** Structural equality. Constants are compared by value. */
int expr_equal(const expr_t *a, const expr_t *b);

/**
 * Callback for expr_map. Returns the replacement for node, the node itself
 * to keep it, or NULL after reporting an error.
 */
typedef expr_t *(*expr_map_fn_t)(cas_t *cas, expr_t *node, void *user);

/**
 * Rewrites a tree bottom-up. Children are mapped first, then fn runs on the
 * node, which is rebuilt only if a child changed. Returns NULL if any call
 * to fn failed.
 */
expr_t *expr_map(cas_t *cas, expr_t *e, expr_map_fn_t fn, void *user);

#endif // EXPR_H
