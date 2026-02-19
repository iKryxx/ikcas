//
// Created by ikryxxdev on 2/13/26.
//

#ifndef IKCAS_EVAL_H
#define IKCAS_EVAL_H
#include "env.h"
#include "node.h"


typedef struct {
    bool ok;
    const char* err;
    node_t* out;
} eval_result_t;

eval_result_t node_eval_exact(arena_t *a, node_t *n, env_t *env);
eval_result_t node_approx(arena_t *a, const node_t *n);
eval_result_t node_eval_approx(arena_t *a, node_t *n, env_t *env);




#endif //IKCAS_EVAL_H
