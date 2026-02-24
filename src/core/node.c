//
// Created by ikryxxdev on 2/12/26.
//

#include "core/node.h"

#include <string.h>

#include "core/builtins.h"

static node_t *node_alloc(arena_t *a) { return arena_alloc(a, sizeof(node_t)); }

node_t *node_rat(arena_t *a, rat_t r) {
    node_t *n = node_alloc(a);
    n->kind = NODE_RAT;
    n->rat = r;
    return n;
}

node_t *node_real(arena_t *a, double v) {
    node_t *n = node_alloc(a);
    n->kind = NODE_REAL;
    n->real = v;
    return n;
}

node_t *node_symbol(arena_t *a, const char *s, int len) {
    if (len < 0)
        len = (int)strlen(s);
    node_t *n = node_alloc(a);
    n->kind = NODE_SYMBOL;
    n->symbol = arena_strdup(a, s, len);
    return n;
}

node_t *node_unary(arena_t *a, node_kind_t kind, node_t *a_) {
    node_t *n = node_alloc(a);
    n->kind = kind;
    n->unary.a = a_;
    return n;
}

node_t *node_bin(arena_t *a, node_kind_t kind, node_t *a_, node_t *b_) {
    node_t *n = node_alloc(a);
    n->kind = kind;
    n->bin.a = a_;
    n->bin.b = b_;
    return n;
}

node_t *node_call(arena_t *a, const char *name, node_t **args, int argc) {
    node_t *n = node_alloc(a);
    n->kind = NODE_CALL;
    n->call.name = name;
    n->call.args = args;
    n->call.argc = argc;
    return n;
}

node_t *node_callable_builtin(arena_t *a, const builtin_fn_t *fn) {
    callable_t *c = (callable_t *)arena_alloc(a, sizeof(callable_t));
    if (!c)
        return nullptr;
    c->kind = CALL_BUILTIN;
    c->min_arity = fn->min_arity;
    c->max_arity = fn->max_arity;
    c->builtin = fn;

    node_t *n = node_alloc(a);
    if (!n)
        return nullptr;
    n->kind = NODE_CALLABLE;
    n->callable = c;
    return n;
}

node_t *node_callable_user(arena_t *a, const user_fn_t *uf) {
    callable_t *c = (callable_t *)arena_alloc(a, sizeof(callable_t));
    if (!c)
        return nullptr;
    c->kind = CALL_USER;
    c->min_arity = uf->arity;
    c->max_arity = uf->arity;
    c->user = uf;

    node_t *n = node_alloc(a);
    if (!n)
        return nullptr;
    n->kind = NODE_CALLABLE;
    n->callable = c;
    return n;
}

const node_t *node_deep_copy(arena_t *a, const node_t *n) {
    node_t *out = node_alloc(a);
    if (!out)
        return nullptr;
    *out = *n;

    switch (n->kind) {
    case NODE_SYMBOL:
        out->symbol = arena_strdup(a, n->symbol, -1);
        break;
    case NODE_NEG:
        out->unary.a = (node_t *)node_deep_copy(a, n->unary.a);
        break;
    case NODE_ADD:
    case NODE_SUB:
    case NODE_MUL:
    case NODE_DIV:
    case NODE_POW:
        out->bin.a = (node_t *)node_deep_copy(a, n->bin.a);
        out->bin.b = (node_t *)node_deep_copy(a, n->bin.b);
        break;
    case NODE_CALL: {
        out->call.name = arena_strdup(a, n->call.name, -1);
        node_t **args =
            (node_t **)arena_alloc(a, n->call.argc * sizeof(node_t *));
        if (!args)
            return nullptr;
        for (int i = 0; i < n->call.argc; i++) {
            args[i] = (node_t *)node_deep_copy(a, n->call.args[i]);
        }
        out->call.args = args;
        break;
    }
    default:
        break;
    }
    return out;
}
