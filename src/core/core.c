//
// Created by ikryxxdev on 2/10/26.
//

#include "core/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/env.h"
#include "core/eval.h"
#include "core/node.h"
#include "core/parse.h"
#include "core/print.h"
#include "memory/arena.h"

static env_t    g_env;
static arena_t  g_store;
static eval_mode_t g_eval_mode = EVAL_MODE_EXACT;

bool print_ast = false;

static const node_t *store_copy_node(const node_t *n);

static node_t * fn_abs_cb(arena_t* a, node_t** args, int argc, lookup_fn_t lookup, void* lctx, const char** err) {
    (void) lookup;
    (void) lctx;
    if (argc != 1) {
        if (err) *err = "abs takes 1 argument";
        return nullptr;
    }

    node_t* x = args[0];

    if (x->kind == NODE_RAT) {
        rat_t r = rat_norm(x->rat);
        if (r.num < 0) r.num = -r.num;
        return node_rat(a, r);
    }
    if (x->kind == NODE_REAL) {
        return node_real(a, x->real < 0.0 ? -x->real : x->real);
    }
    if (x->kind == NODE_NEG) {
        node_t* inner = x->unary.a;

        if (inner->kind == NODE_RAT) {
            rat_t r = rat_norm(inner->rat);
            if (r.num < 0) r.num = -r.num;
            return node_rat(a, r);
        }
        if (inner->kind == NODE_REAL) {
            return node_real(a, inner->real < 0.0 ? -inner->real : inner->real);
        }

        node_t** one = arena_alloc(a, sizeof(node_t*));
        if (!one) {
            if (err) *err = "out of memory";
            return nullptr;
        }
        one[0] = inner;
        return node_call(a, arena_strdup(a, "abs", -1), one, 1);
    }

    return nullptr;
}
static const builtin_fn_t FN_ABS = {
    "abs",
    1,
    1,
    fn_abs_cb
};

void core_init(void) {
    env_init(&g_env);
    arena_init(&g_store);

    node_t* pi = node_symbol(&g_store, arena_strdup(&g_store, "pi", -1), -1);
    node_t* e = node_symbol(&g_store, arena_strdup(&g_store, "e", -1), -1);
    env_set(&g_env, "pi", pi);
    env_set(&g_env, "e", e);

    node_t* abs = node_callable_builtin(&g_store, &FN_ABS);
    env_set(&g_env, "abs", abs);
}

void core_shutdown(void) {
    env_free(&g_env);
    arena_destroy(&g_store);
}

static const node_t * store_copy_node_rec(const node_t* n) {
    node_t *out = arena_alloc(&g_store, sizeof(*out));
    if (!out) return nullptr;
    *out = *n;

    switch (n->kind) {
        case NODE_SYMBOL:
            out->symbol = arena_strdup(&g_store, n->symbol, -1);
            break;
        case NODE_NEG:
            out->unary.a = (node_t*)store_copy_node_rec(n->unary.a);
            break;
        case NODE_ADD:
        case NODE_SUB:
        case NODE_MUL:
        case NODE_DIV:
        case NODE_POW:
            out->bin.a = (node_t*)store_copy_node_rec(n->bin.a);
            out->bin.b = (node_t*)store_copy_node_rec(n->bin.b);
            break;
        case NODE_CALL:
            out->call.name = arena_strdup(&g_store, n->call.name, -1);
            node_t **args = (node_t**)arena_alloc(&g_store, n->call.argc * sizeof(node_t*));
            if (!args) return nullptr;
            for (int i = 0; i < n->call.argc; i++) {
                args[i] = (node_t*)store_copy_node_rec(n->call.args[i]);
            }
            out->call.args = args;
            break;
        default:
            break;
    }

    return out;
}

static const node_t *store_copy_node(const node_t *n) {
    return store_copy_node_rec(n);
}

static node_t* store_copy_userfunc(const char* name, const char** params_parse_arena, int arity, const node_t* body_parse_arena) {
    const char** params = arena_alloc(&g_store, arity * sizeof(const char*));
    if (!params) return nullptr;
    for (int i = 0; i < arity; i++) {
        params[i] = arena_strdup(&g_store, params_parse_arena[i], -1);
        if (!params[i]) return nullptr;
    }

    const node_t* body = node_deep_copy(&g_store, body_parse_arena);
    if (!body) return nullptr;

    user_fn_t* uf = arena_alloc(&g_store, sizeof(user_fn_t));
    if (!uf) return nullptr;
    uf->arity = arity;
    uf->params = params;
    uf->body = body;

    node_t* callable = node_callable_user(&g_store, uf);
    if (!callable) return nullptr;

    if (!env_set(&g_env, name, callable)) return nullptr;

    return callable;
}

core_result_t core_eval(const char *expr) {
    core_result_t out;
    memset(&out, 0, sizeof(out));

    arena_t a;
    arena_init(&a);

    const char **out_text = (const char**)out.text;
    stmt_t statement = parse_stmnt(&a, expr, out_text);
    if (!statement.name && !statement.expr) {
        memccpy(out.text, *out_text, '\0', sizeof(out.text));
        out.ok = false;
        arena_destroy(&a);
        return out;
    }

    if (statement.kind == STMT_ASSIGN) {
        out.kind = CORE_ASSIGNMENT;
        if (statement.name) {
            env_set(&g_env, statement.name, (node_t*)store_copy_node(statement.expr));
        }
    }
    else if (statement.kind == STMT_FUNCDEF) {
        out.kind = CORE_ASSIGNMENT;
        if (statement.name) {
            store_copy_userfunc(statement.name, statement.params, statement.arity, statement.expr);
        }
    }



    sb_t sb;
    sb_init(&sb, out.text, (int)sizeof(out.text));

    if (print_ast)
        node_print(&sb, statement.expr, 0);
    else if (statement.kind == STMT_EXPR) {
        eval_result_t res = (g_eval_mode == EVAL_MODE_APPROX)
                                ? node_eval_approx(&a, statement.expr, &g_env)
                                : node_eval_exact(&a, statement.expr, &g_env);
        out.ok = res.ok;
        out.kind = CORE_EVALUATION;

        if (!res.ok) {
            out.kind = CORE_ERROR;
            snprintf(out.text, sizeof(out.text), "%s", res.err);
            arena_destroy(&a);
            return out;
        }
        node_print(&sb, res.out, 0);
    }

    out.ok = true;
    arena_destroy(&a);
    return out;
}

void core_set_eval_mode(eval_mode_t mode) {
    g_eval_mode = mode;
}

eval_mode_t core_get_eval_mode(void) {
    return g_eval_mode;
}
