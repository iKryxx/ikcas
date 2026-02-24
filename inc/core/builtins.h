//
// Created by ikryxxdev on 2/23/26.
//

#ifndef IKCAS_BUILTINS_H
#define IKCAS_BUILTINS_H

typedef struct node node_t;
typedef struct arena arena_t;
typedef bool (*lookup_fn_t)(void *ctx, const char *name, node_t **out);

typedef struct builtin_fn {
    const char *name;
    int min_arity;
    int max_arity;
    node_t *(*eval_exact)(arena_t *a, node_t **args, int argc,
                          lookup_fn_t lookup, void *lctx, const char **err);
} builtin_fn_t;

node_t *fn_abs_cb(arena_t *a, node_t **args, int argc, lookup_fn_t lookup,
                  void *lctx, const char **err);
extern builtin_fn_t FN_ABS;

#endif // IKCAS_BUILTINS_H
