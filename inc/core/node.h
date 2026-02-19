//
// Created by ikryxxdev on 2/12/26.
//

#ifndef IKCAS_NODE_H
#define IKCAS_NODE_H
#include "env.h"
#include "num.h"
#include "memory/arena.h"

typedef enum {
    NODE_RAT,
    NODE_REAL,
    NODE_SYMBOL,
    NODE_NEG,

    NODE_ADD,
    NODE_SUB,
    NODE_MUL,
    NODE_DIV,
    NODE_POW,

    NODE_CALL,

    NODE_CALLABLE
} node_kind_t;
typedef enum {
    CALL_BUILTIN,
    CALL_USER
} call_kind_t;

typedef struct builtin_fn builtin_fn_t;
typedef struct user_fn user_fn_t;
typedef struct callable callable_t;

typedef struct node {
    node_kind_t kind;

    union {
        rat_t rat;
        double real;
        const char *symbol;

        struct {struct node *a;} unary;
        struct {struct node *a, *b;} bin;

        struct { const char *name; struct node **args; int argc; } call;

        const callable_t *callable;
    };
} node_t;

struct builtin_fn {
    const char* name;
    int min_arity;
    int max_arity;
    node_t* (*eval_exact)(arena_t *a, node_t** args, int argc, lookup_fn_t lookup, void* lctx, const char** err);
};

struct user_fn {
    const char** params;
    int arity;
    const node_t *body;
};

struct callable {
    call_kind_t kind;
    int min_arity, max_arity;
    union {
        const builtin_fn_t *builtin;
        const user_fn_t *user;
    };
};

node_t *node_rat(arena_t *a, rat_t r);
node_t *node_real(arena_t *a, double v);
node_t *node_symbol(arena_t *a, const char *s, int len);
node_t *node_unary(arena_t *a, node_kind_t kind, node_t *a_);
node_t *node_bin(arena_t *a, node_kind_t kind, node_t *a_, node_t *b_);
node_t *node_call(arena_t *a, const char *name, node_t **args, int argc);

node_t *node_callable_builtin(arena_t *a, const builtin_fn_t *fn);
node_t *node_callable_user(arena_t *a, const user_fn_t *uf);

const node_t *node_deep_copy(arena_t *a, const node_t *n);

void dump_tree(arena_t *a);
#endif //IKCAS_NODE_H