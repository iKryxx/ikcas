#include "engine/bindings.h"
#include "engine/cas.h"
#include "expr/expr.h"

#include <stdlib.h>
#include <string.h>

/**
 * Index of the binding called name, or -1.
 * Example: after binding_declare(cas, "ans", ...), find_index(t, "ans", 3) is 0.
 */
static long find_index(const binding_table_t *t, const char *name, size_t len)
{
    for (size_t i = 0; i < t->len; i++) {
        if (t->items[i].len == len && memcmp(t->items[i].name, name, len) == 0)
            return (long)i;
    }
    return -1;
}

/**
 * Appends an unset binding and returns it, or NULL when out of memory.
 * Example: push(cas, "a", 1, 0) adds `a` with no value.
 */
static binding_t *push(cas_t *cas, const char *name, size_t len, unsigned flags)
{
    binding_table_t *t = &cas->bindings;
    if (t->len == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 8;
        binding_t *p = realloc(t->items, cap * sizeof(*p));
        if (!p) {
            cas_error(cas, CAS_ERR_NOMEM, "out of memory");
            return NULL;
        }
        t->items = p;
        t->cap = cap;
    }
    char *copy = malloc(len + 1);
    if (!copy) {
        cas_error(cas, CAS_ERR_NOMEM, "out of memory");
        return NULL;
    }
    memcpy(copy, name, len);
    copy[len] = '\0';

    binding_t *b = &t->items[t->len++];
    *b = (binding_t){ copy, len, NULL, flags };
    return b;
}

/** Reserves a name without a value. Example: binding_declare(cas, "ans", BIND_READONLY). */
cas_status_t binding_declare(cas_t *cas, const char *name, unsigned flags)
{
    size_t len = strlen(name);
    if (find_index(&cas->bindings, name, len) >= 0)
        return cas_error(cas, CAS_ERR_REGISTRY, "binding '%s' already declared", name);
    return push(cas, name, len, flags) ? CAS_OK : CAS_ERR_NOMEM;
}

/** The binding called name, or NULL. Example: binding_find(cas, "ans", 3). */
const binding_t *binding_find(const cas_t *cas, const char *name, size_t len)
{
    long i = find_index(&cas->bindings, name, len);
    return i >= 0 ? &cas->bindings.items[i] : NULL;
}

/** Replaces the value of name with a heap copy of value. Example: binding_set(cas, "ans", 3, res.result). */
cas_status_t binding_set(cas_t *cas, const char *name, size_t len, const expr_t *value)
{
    // Copy first and swap after, so a failed copy leaves the old value usable.
    expr_t *copy = expr_persist(cas, value);
    if (!copy)
        return cas->diags.len ? cas->diags.items[cas->diags.len - 1].status : CAS_ERR_NOMEM;

    long i = find_index(&cas->bindings, name, len);
    binding_t *b = i >= 0 ? &cas->bindings.items[i] : push(cas, name, len, 0);
    if (!b) {
        expr_persist_free(copy);
        return CAS_ERR_NOMEM;
    }
    expr_persist_free(b->value);
    b->value = copy;
    return CAS_OK;
}

/** Frees every binding and its value. Example: called once from cas_free. */
void binding_table_destroy(binding_table_t *t)
{
    for (size_t i = 0; i < t->len; i++) {
        free(t->items[i].name);
        expr_persist_free(t->items[i].value);
    }
    free(t->items);
    memset(t, 0, sizeof(*t));
}
