//
// Created by ikryxxdev on 2/13/26.
//

#include "core/eval.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/builtins.h"
#include "logging/log.h"

static eval_result_t eval_impl(arena_t *a, const node_t *n, lookup_fn_t lookup,
                               void *lctx);
static eval_result_t eval_symbol_impl(arena_t *a, const node_t *n,
                                      lookup_fn_t lookup, void *lctx);
static eval_result_t eval_call_impl(arena_t *a, const node_t *n,
                                    lookup_fn_t lookup, void *lctx);
static eval_result_t resolve_callable_impl(const char *name, lookup_fn_t lookup,
                                           void *lctx,
                                           const node_t **out_callable);
static eval_result_t eval_binop_impl(arena_t *a, node_kind_t kind,
                                     const node_t *l, const node_t *r,
                                     lookup_fn_t lookup, void *lctx);
static eval_result_t approx_impl(arena_t *a, const node_t *n,
                                 lookup_fn_t lookup, void *lctx);

static eval_result_t ok(node_t *n) {
    return (eval_result_t){.ok = true, .err = nullptr, .out = n};
}
static eval_result_t fail(const char *err) {
    return (eval_result_t){.ok = false, .err = err, .out = nullptr};
}

static bool is_rat(const node_t *n) { return n && n->kind == NODE_RAT; }
static bool is_zero_rat(const node_t *n) {
    return is_rat(n) && rat_is_zero(rat_norm(n->rat));
}

static bool is_one_rat(const node_t *n) {
    return is_rat(n) && rat_norm(n->rat).num == rat_norm(n->rat).den;
}
static node_t *mk_rat(arena_t *a, rat_t r) { return node_rat(a, rat_norm(r)); }
static node_t *mk_real(arena_t *a, double r) { return node_real(a, r); }
static node_t *mk_neg(arena_t *a, node_t *n) {
    if (is_rat(n)) {
        rat_t r = rat_norm(n->rat);
        r.num = -r.num;
        return mk_rat(a, r);
    }
    return node_unary(a, NODE_NEG, n);
}

static node_t *mk_bin(arena_t *a, node_kind_t kind, node_t *a_, node_t *b_) {
    return node_bin(a, kind, a_, b_);
}

static eval_result_t eval(arena_t *a, const node_t *n, env_t *env);

typedef struct {
    const char *name;
    double value;
} approx_const_t;

static const approx_const_t APPROX_CONSTS[] = {
    {"pi", 3.14159265358979323846},
    {"e", 2.71828182845904523536},
};

static bool approx_const_lookup(const char *name, double *out) {
    for (size_t i = 0; i < sizeof(APPROX_CONSTS) / sizeof(APPROX_CONSTS[0]);
         i++) {
        if (strcmp(APPROX_CONSTS[i].name, name) == 0) {
            if (out)
                *out = APPROX_CONSTS[i].value;
            return true;
        }
    }
    return false;
}

static eval_result_t eval_impl(arena_t *a, const node_t *n, lookup_fn_t lookup,
                               void *lctx) {
    if (!n)
        return fail("null node");

    switch (n->kind) {
    case NODE_RAT:
        return ok(mk_rat(a, n->rat));
    case NODE_REAL:
        return ok(mk_real(a, n->real));
    case NODE_SYMBOL:
        return eval_symbol_impl(a, n, lookup, lctx);
    case NODE_NEG: {
        eval_result_t x = eval_impl(a, n->unary.a, lookup, lctx);
        if (!x.ok)
            return x;
        return ok(mk_neg(a, x.out));
    }
    case NODE_ADD:
    case NODE_SUB:
    case NODE_MUL:
    case NODE_DIV:
    case NODE_POW:
        return eval_binop_impl(a, n->kind, n->bin.a, n->bin.b, lookup, lctx);
    case NODE_CALL:
        return eval_call_impl(a, n, lookup, lctx);
    case NODE_CALLABLE:
        return ok((node_t *)n);
    default:
        UNREACHABLE();
    }
}

static eval_result_t eval_symbol_impl(arena_t *a, const node_t *n,
                                      lookup_fn_t lookup, void *lctx) {
    if (!n || n->kind != NODE_SYMBOL)
        return fail("not a symbol");

    const char *seen[64];
    int sp = 0;
    const node_t *cur = n;

    while (cur->kind == NODE_SYMBOL) {
        const char *name = cur->symbol;

        for (int i = 0; i < sp; i++) {
            if (strcmp(name, seen[i]) == 0)
                return fail("cyclic definition");
        }
        if (sp >= 64)
            return fail("definitions too deep");

        seen[sp++] = name;
        node_t *b = nullptr;
        if (!lookup || !lookup(lctx, name, &b) || !b) {
            return fail("undefined variable");
        }

        if (b->kind == NODE_SYMBOL && strcmp(b->symbol, name) == 0) {
            return ok(node_symbol(a, name, -1));
        }

        cur = b;
    }

    return eval_impl(a, cur, lookup, lctx);
}

static eval_result_t eval_call_impl(arena_t *a, const node_t *n,
                                    lookup_fn_t lookup, void *lctx) {
    const node_t *callee = nullptr;
    eval_result_t rc =
        resolve_callable_impl(n->call.name, lookup, lctx, &callee);
    if (!rc.ok)
        return rc;

    const callable_t *c = callee->callable;
    const int argc = n->call.argc;

    if (argc < c->min_arity || argc > c->max_arity) {
        return fail("wrong arity");
    }

    node_t **args = arena_alloc(a, argc * sizeof(node_t *));
    if (!args)
        return fail("out of memory");

    for (int i = 0; i < argc; i++) {
        eval_result_t ei = eval_impl(a, n->call.args[i], lookup, lctx);
        if (!ei.ok)
            return ei;
        args[i] = ei.out;
        if (!args[i])
            return fail("null arg");
    }

    if (c->kind == CALL_BUILTIN) {
        const builtin_fn_t *bf = c->builtin;
        const char *berr = nullptr;

        node_t *folded =
            bf->eval_exact ? bf->eval_exact(a, args, argc, lookup, lctx, &berr)
                           : nullptr;

        if (berr)
            return fail(berr);
        if (folded)
            return ok(folded);

        const char *name_dup = arena_strdup(a, n->call.name, -1);
        if (!name_dup)
            return fail("out of memory");
        return ok(node_call(a, name_dup, args, argc));
    }

    if (c->kind == CALL_USER) {
        const user_fn_t *uf = c->user;
        if (argc != uf->arity)
            return fail("wrong arity");

        const char **names = arena_alloc(a, argc * sizeof(const char *));
        const node_t **values = arena_alloc(a, argc * sizeof(const node_t *));
        if (!names || !values)
            return fail("out of memory");

        for (int i = 0; i < argc; i++) {
            names[i] = uf->params[i];
            values[i] = args[i];
        }

        env_overlay_t o = {0};
        o.base_ctx = lctx;
        o.base_lookup = lookup;
        o.names = names;
        o.values = values;
        o.len = argc;

        return eval_impl(a, uf->body, lookup_overlay, &o);
    }

    return fail("call target not supported");
}

static eval_result_t resolve_callable_impl(const char *name, lookup_fn_t lookup,
                                           void *lctx,
                                           const node_t **out_callable) {
    const char *seen[64];
    int sp = 0;
    const char *cur = name;

    for (;;) {
        for (int i = 0; i < sp; i++) {
            if (strcmp(seen[i], cur) == 0)
                return fail("cyclic definition");
        }
        if (sp >= 64)
            return fail("definitions too deep");
        seen[sp++] = cur;

        node_t *b = nullptr;
        if (!lookup || !lookup(lctx, cur, &b) || !b) {
            return fail("undefined function");
        }

        if (b->kind == NODE_SYMBOL && strcmp(b->symbol, cur) == 0) {
            return fail("undefined function");
        }

        if (b->kind == NODE_SYMBOL) {
            cur = b->symbol;
            continue;
        }
        if (b->kind == NODE_CALLABLE) {
            *out_callable = b;
            return ok(nullptr);
        }

        return fail("symbol is not callable");
    }
}

static eval_result_t eval_binop_impl(arena_t *a, node_kind_t kind,
                                     const node_t *l, const node_t *r,
                                     lookup_fn_t lookup, void *lctx) {
    eval_result_t L = eval_impl(a, l, lookup, lctx);
    if (!L.ok)
        return L;
    eval_result_t R = eval_impl(a, r, lookup, lctx);
    if (!R.ok)
        return R;

    node_t *l_ = L.out;
    node_t *r_ = R.out;

    switch (kind) {
    case NODE_ADD:
        if (is_zero_rat(l_))
            return ok(r_);
        if (is_zero_rat(r_))
            return ok(l_);
        if (is_rat(l_) && is_rat(r_))
            return ok(mk_rat(a, rat_add(l_->rat, r_->rat)));
        return ok(mk_bin(a, NODE_ADD, l_, r_));
    case NODE_SUB:
        if (is_zero_rat(r_))
            return ok(l_);
        if (is_rat(l_) && is_rat(r_))
            return ok(mk_rat(a, rat_sub(l_->rat, r_->rat)));
        if (is_zero_rat(l_))
            return ok(mk_neg(a, r_));
        return ok(mk_bin(a, NODE_SUB, l_, r_));
    case NODE_MUL:
        if (is_zero_rat(l_) || is_zero_rat(r_))
            return ok(mk_rat(a, rat_from_i64(0)));
        if (is_one_rat(l_))
            return ok(r_);
        if (is_one_rat(r_))
            return ok(l_);
        if (is_rat(l_) && is_rat(r_))
            return ok(mk_rat(a, rat_mul(l_->rat, r_->rat)));
        if (!is_rat(l_) && is_rat(r_))
            return ok(mk_bin(a, NODE_MUL, r_, l_));
        return ok(mk_bin(a, NODE_MUL, l_, r_));
    case NODE_DIV:
        if (is_zero_rat(l_)) {
            if (is_zero_rat(r_)) {
                return fail("division by zero");
            }
            return ok(mk_rat(a, rat_from_i64(0)));
        }
        if (is_one_rat(r_))
            return ok(l_);
        if (is_rat(l_) && is_rat(r_)) {
            bool okd = true;
            rat_t q = rat_div(l_->rat, r_->rat, &okd);
            if (!okd)
                return fail("division by zero");
            return ok(mk_rat(a, q));
        }
        return ok(mk_bin(a, NODE_DIV, l_, r_));
    case NODE_POW:
        if (is_rat(l_) && is_rat(r_)) {
            rat_t e = rat_norm(r_->rat);
            if (e.den == 1) {
                bool okp = true;
                rat_t pw = rat_pow_i64(l_->rat, e.num, &okp);
                if (!okp)
                    return fail("invalid power");
                return ok(mk_rat(a, pw));
            }
        }
        return ok(mk_bin(a, NODE_POW, l_, r_));
    default:
        return fail("unknown binop"); // Unreachable
    }
}

static eval_result_t eval(arena_t *a, const node_t *n, env_t *env) {
    return eval_impl(a, n, lookup_env, env);
}
eval_result_t node_eval_exact(arena_t *a, node_t *n, env_t *env) {
    return eval(a, n, env);
}

static bool is_real(const node_t *n) { return n && n->kind == NODE_REAL; }
static node_t *mk_real_bin(arena_t *a, node_kind_t kind, double lhs, double rhs,
                           const char **err) {
    switch (kind) {
    case NODE_ADD:
        return node_real(a, lhs + rhs);
    case NODE_SUB:
        return node_real(a, lhs - rhs);
    case NODE_MUL:
        return node_real(a, lhs * rhs);
    case NODE_DIV:
        if (rhs == 0.0) {
            if (err)
                *err = "division by zero";
            return nullptr;
        }
        return node_real(a, lhs / rhs);
    case NODE_POW:
        return node_real(a, pow(lhs, rhs));
    default:
        return nullptr;
    }
}

static eval_result_t approx_impl(arena_t *a, const node_t *n,
                                 lookup_fn_t lookup, void *lctx) {
    if (!n)
        return fail("null node");

    switch (n->kind) {
    case NODE_RAT:
        return ok(node_real(a, (double)n->rat.num / (double)n->rat.den));
    case NODE_REAL:
        return ok(node_real(a, n->real));
    case NODE_SYMBOL: {
        double v = 0.0;
        if (approx_const_lookup(n->symbol, &v))
            return ok(node_real(a, v));
        return ok(node_symbol(a, n->symbol, -1));
    }
    case NODE_NEG: {
        eval_result_t x = approx_impl(a, n->unary.a, lookup, lctx);
        if (!x.ok)
            return x;
        if (is_real(x.out))
            return ok(node_real(a, -x.out->real));
        return ok(node_unary(a, NODE_NEG, x.out));
    }
    case NODE_ADD:
    case NODE_SUB:
    case NODE_MUL:
    case NODE_DIV:
    case NODE_POW: {
        eval_result_t l = approx_impl(a, n->bin.a, lookup, lctx);
        if (!l.ok)
            return l;
        eval_result_t r = approx_impl(a, n->bin.b, lookup, lctx);
        if (!r.ok)
            return r;

        if (is_real(l.out) && is_real(r.out)) {
            const char *err = nullptr;
            node_t *out =
                mk_real_bin(a, n->kind, l.out->real, r.out->real, &err);
            if (!out)
                return fail(err ? err : "approximation failed");
            return ok(out);
        }

        return ok(node_bin(a, n->kind, l.out, r.out));
    }
    case NODE_CALL: {
        node_t **args = arena_alloc(a, n->call.argc * sizeof(node_t *));
        if (!args)
            return fail("out of memory");
        for (int i = 0; i < n->call.argc; i++) {
            eval_result_t ai = approx_impl(a, n->call.args[i], lookup, lctx);
            if (!ai.ok)
                return ai;
            args[i] = ai.out;
        }

        if (lookup) {
            const node_t *callee = nullptr;
            eval_result_t rc =
                resolve_callable_impl(n->call.name, lookup, lctx, &callee);
            if (!rc.ok)
                return rc;

            const callable_t *c = callee->callable;
            const int argc = n->call.argc;
            if (argc < c->min_arity || argc > c->max_arity)
                return fail("wrong arity");

            if (c->kind == CALL_BUILTIN) {
                const builtin_fn_t *bf = c->builtin;
                const char *berr = nullptr;
                node_t *folded =
                    bf->eval_exact
                        ? bf->eval_exact(a, args, argc, lookup, lctx, &berr)
                        : nullptr;
                if (berr)
                    return fail(berr);
                if (folded)
                    return approx_impl(a, folded, lookup, lctx);
            } else if (c->kind == CALL_USER) {
                const user_fn_t *uf = c->user;
                if (argc != uf->arity)
                    return fail("wrong arity");

                const char **names =
                    arena_alloc(a, argc * sizeof(const char *));
                const node_t **values =
                    arena_alloc(a, argc * sizeof(const node_t *));
                if (!names || !values)
                    return fail("out of memory");

                for (int i = 0; i < argc; i++) {
                    names[i] = uf->params[i];
                    values[i] = args[i];
                }

                env_overlay_t o = {0};
                o.base_ctx = lctx;
                o.base_lookup = lookup;
                o.names = names;
                o.values = values;
                o.len = argc;

                eval_result_t ev = eval_impl(a, uf->body, lookup_overlay, &o);
                if (!ev.ok)
                    return ev;
                return approx_impl(a, ev.out, lookup_overlay, &o);
            }
        }

        return ok(node_call(a, arena_strdup(a, n->call.name, -1), args,
                            n->call.argc));
    }
    case NODE_CALLABLE:
        return ok((node_t *)n);
    default:
        return fail("unknown node");
    }
}

eval_result_t node_approx(arena_t *a, const node_t *n) {
    return approx_impl(a, n, NULL, NULL);
}

eval_result_t node_eval_approx(arena_t *a, node_t *n, env_t *env) {
    eval_result_t ex = node_eval_exact(a, n, env);
    if (!ex.ok)
        return ex;
    return approx_impl(a, ex.out, lookup_env, env);
}
