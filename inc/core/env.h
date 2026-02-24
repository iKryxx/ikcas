//
// Created by ikryxxdev on 2/10/26.
//

#ifndef IKCAS_ENV_H
#define IKCAS_ENV_H

#include <stdbool.h>

typedef struct node node_t;
typedef bool (*lookup_fn_t)(void *ctx, const char *name, node_t **out);
typedef struct {
    bool is_constant;
    const char *name;
    node_t *value;
} env_entry_t;

typedef struct env {
    env_entry_t *entries;
    int len;
    int cap;
} env_t;

typedef struct {
    void *base_ctx;
    lookup_fn_t base_lookup;
    const char **names;
    const node_t **values;
    int len;
} env_overlay_t;

bool lookup_env(void *ctx, const char *name, node_t **out);
bool lookup_overlay(void *ctx, const char *name, node_t **out);

void env_init(env_t *env);
void env_free(env_t *env);
bool env_get(const env_t *env, const char *name, node_t **out);
bool env_remove(env_t *env, const char *name);
bool env_set(env_t *env, const char *name, node_t *value, bool is_constant);

bool env_overlay_get(const env_overlay_t *o, const char *name, node_t **out);
bool env_overlay_get_const(const env_overlay_t *o, const char *name,
                           const node_t **out);
#endif // IKCAS_ENV_H
