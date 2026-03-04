//
// Created by ikryxxdev on 2/23/26.
//

#include "core/builtins.h"

#include "core/core.h"
#include "core/node.h"
#include "memory/arena.h"
#include <math.h>
#include <string.h>

#define unused(x) ((void)(x))

#define on_rat(x) if (x->kind == NODE_RAT)
#define on_real(x) if (x->kind == NODE_REAL)
#define on_neg(x) if (x->kind == NODE_NEG)
#define on_symbol_name(x, n)                                                   \
    if (x->kind == NODE_SYMBOL && strcmp(x->symbol, n) == 0)
#define on_symbol(x) if (x->kind == NODE_SYMBOL)
#define on_mul(x) if (x->kind == NODE_MUL)
#define on_div(x) if (x->kind == NODE_DIV)

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

static bool extract_pi_multiple(const node_t *n, rat_t *out) {
    // k*pi/d

    on_symbol_name(n, "pi") { // pi
        *out = rat_from_i64(1);
        return true;
    }
    on_mul(n) { // k*pi
        const node_t *k = n->bin.a;
        const node_t *b = n->bin.b;

        if (k->kind == NODE_RAT && b->kind == NODE_SYMBOL &&
            strcmp(b->symbol, "pi") == 0) {
            *out = k->rat;
            return true;
        }
    }
    on_div(n) {
        const node_t *kb = n->bin.a;
        const node_t *d = n->bin.b;

        on_mul(kb) { // k*pi/d
            const node_t *k = kb->bin.a;
            const node_t *b = kb->bin.b;

            if (b->kind == NODE_SYMBOL && k->kind == NODE_RAT &&
                strcmp(b->symbol, "pi") == 0) {
                *out = k->rat;
                out->den = d->rat.num;
                return true;
            }
        }

        on_symbol_name(kb, "pi") { // pi/d
            *out = d->rat;
            return true;
        }
    }
    return false;
}

// ---------- Sin: take the sine of node. 1 Arg ----------
fn_cb_decl(fn_sin_cb) {
    unused(lookup);
    unused(lctx);
    check_arity(1, 1, argc, "sin", err);

    const node_t *x = args[0];
    rat_t r;

    // First step: try to reduce the arguments and find special values
    if (extract_pi_multiple(x, &r)) {
        r = rat_mod(r, rat_from_i64(2), nullptr);

        rat_t rn = rat_norm(r);

        // sin(n*pi) = 0
        if (rn.den == 1) {
            return node_rat(a, rat_from_i64(0));
        }

        // sin(k*pi/2)
        rat_t k = rat_mul(r, rat_from_i64(2));
        k = rat_norm(k);

        if (k.den == 1) {
            long n = k.num % 4;
            if (n < 0)
                n += 4;

            switch (n) {
            case 0:
                return node_rat(a, rat_from_i64(0));
            case 1:
                return node_rat(a, rat_from_i64(1));
            case 2:
                return node_rat(a, rat_from_i64(0));
            case 3:
                return node_rat(a, rat_from_i64(-1));
            }
        }
    }

    // evaluate inner and try to compute sine
    if (core_get_eval_mode() == EVAL_MODE_APPROX) {
        on_real(x) { return node_real(a, sin(x->real)); }
        on_rat(x) {
            return node_real(a, sin((double)x->rat.num / (double)x->rat.den));
        }
        unreachable();
    }

    return nullptr;
}

builtin_fn_t FN_SIN = {"sin", 1, 1, fn_sin_cb};
