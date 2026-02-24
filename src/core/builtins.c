//
// Created by ikryxxdev on 2/23/26.
//

#include "core/builtins.h"

#include "core/node.h"
#include "memory/arena.h"

#define unused(x) ((void)(x))

#define on_rat(x) if (x->kind == NODE_RAT)
#define on_real(x) if (x->kind == NODE_REAL)
#define on_neg(x) if (x->kind == NODE_NEG)

static bool check_arity(int min, int max, int got, const char *name,
                        const char **err) {
    if (min > max)
        return false;
    if (min <= got && max >= got)
        return true;
    if (!err)
        return false;
    if (min == max) {
        sprintf(*(char **)err, "%s takes %d argument.", name, min);
    } else {
        sprintf(*(char **)err, "%s takes between %d and %d arguments.", name,
                min, max);
    }
    return false;
}

// ---------- Abs: take the absolute value of node. 1 Arg ----------
node_t *fn_abs_cb(arena_t *a, node_t **args, int argc, lookup_fn_t lookup,
                  void *lctx, const char **err) {
    unused(lookup);
    unused(lctx);
    check_arity(1, 1, argc, "abs", err);

    const node_t *x = args[0];
    on_rat(x) {
        rat_t r = rat_norm(x->rat);
        if (r.num < 0)
            r.num = -r.num;
        return node_rat(a, r);
    }
    on_real(x) { return node_real(a, x->real < 0.0 ? -x->real : x->real); }
    on_neg(x) {
        node_t *inner = x->unary.a;

        on_rat(inner) {
            rat_t r = rat_norm(inner->rat);
            if (r.num < 0)
                r.num = -r.num;
            return node_rat(a, r);
        }
        on_real(inner) {
            return node_real(a, inner->real < 0.0 ? -inner->real : inner->real);
        }

        node_t **one = arena_alloc(a, sizeof(node_t *));
        if (!one) {
            if (err)
                *err = "out of memory";
            return nullptr;
        }
        one[0] = inner;
        return node_call(a, arena_strdup(a, "abs", -1), one, 1);
    }
    return nullptr;
}
builtin_fn_t FN_ABS = {"abs", 1, 1, fn_abs_cb};
