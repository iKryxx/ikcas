//
// Created by ikryxxdev on 2/10/26.
//

#include "core/env.h"
#include "core/node.h"

#include <stdlib.h>
#include <string.h>

bool lookup_env(void *ctx, const char *name, node_t **out) {
    env_t* env = (env_t*)ctx;
    if (!env || !name) return false;
    return env_get(env, name, out);
}

bool lookup_overlay(void *ctx, const char *name, node_t **out) {
    env_overlay_t* o = (env_overlay_t*)ctx;
    return env_overlay_get(o, name, out);
}

void env_init(env_t *env) {
    env->entries = nullptr;
    env->len = 0;
    env->cap = 0;
}

void env_free(env_t *env) {
    if (!env) return;
    for (int i = 0; i < env->len; ++i) free((void*)env->entries[i].name);
    free(env->entries);
    env->entries = nullptr; env->len = env->cap = 0;
}

static char* dupstr(const char* s) {
    size_t len = strlen(s);
    char* res = malloc(len + 1);
    if (!res) return nullptr;
    memcpy(res, s, len + 1);
    return res;
}

bool env_get(const env_t *env, const char *name, node_t **out) {
    for (int i = 0; i < env->len; ++i) {
        if (strcmp(env->entries[i].name, name) == 0) {
            if (out) *out = env->entries[i].value;
            return true;
        }
    }
    return false;
}

bool env_set(env_t *env, const char *name, node_t *value) {
    for (int i = 0; i < env->len; ++i) {
        if (strcmp(env->entries[i].name, name) == 0) {
            env->entries[i].value = value;
            return true;
        }
    }

    if (env->cap == env->len) {
        int ncap = env->cap ? env->cap * 2 : 16;
        void* nv = realloc(env->entries, ncap * sizeof(env_entry_t));
        if (!nv) return false;
        env->entries = nv; env->cap = ncap;
    }

    char* key = dupstr(name);
    if (!key) return false;
    env->entries[env->len++] = (env_entry_t){ key, value };
    return true;
}

bool env_overlay_get(const env_overlay_t *o, const char *name, node_t **out) {
    if (!o || !name) return false;
    for (int i = 0; i < o->len; ++i) {
        if (strcmp(o->names[i], name) == 0) {
            if (out) *out = (node_t*)o->values[i];
            return true;
        }
    }
    if (!o->base_lookup) return false;
    return o->base_lookup(o->base_ctx, name, out);
}

bool env_overlay_get_const(const env_overlay_t *o, const char *name, const node_t **out) {
    node_t* tmp = nullptr;
    if (env_overlay_get(o, name, &tmp)) {
        if (out) *out = tmp;
        return true;
    }
    return false;
}

