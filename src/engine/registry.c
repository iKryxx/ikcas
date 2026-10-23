#include "engine/registry.h"
#include "engine/cas.h"

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/** Zeroes the registry. */
void registry_init(registry_t *r)
{
    memset(r, 0, sizeof(*r));
}

/** Frees a vector's storage. */
static void vec_free(reg_vec_t *v)
{
    free(v->data);
    memset(v, 0, sizeof(*v));
}

/** Frees all vectors. */
void registry_destroy(registry_t *r)
{
    vec_free(&r->types);
    vec_free(&r->fns);
    vec_free(&r->ops);
    vec_free(&r->literals);
    vec_free(&r->constants);
    vec_free(&r->passes);
    vec_free(&r->display_passes);
}

/** Appends an entry, tagged with the plugin that is currently initializing ("core" if none). */
static cas_status_t vec_push(cas_t *cas, reg_vec_t *v, const void *def)
{
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 8;
        reg_entry_t *p = realloc(v->data, cap * sizeof(*p));
        if (!p)
            return cas_error(cas, CAS_ERR_NOMEM, "out of memory");
        v->data = p;
        v->cap = cap;
    }
    v->data[v->len++] = (reg_entry_t){ def, cas->current_plugin ? cas->current_plugin : "core" };
    return CAS_OK;
}

/** Compares a C string with a name that is len bytes long and not terminated. */
static int name_eq(const char *a, const char *b, size_t blen)
{
    return strlen(a) == blen && memcmp(a, b, blen) == 0;
}

// find_name reads the first member of each descriptor, so they all need to
// start with their name.
static_assert(offsetof(value_type_t, name) == 0, "name must come first");
static_assert(offsetof(fn_def_t, name) == 0, "name must come first");
static_assert(offsetof(literal_def_t, name) == 0, "name must come first");
static_assert(offsetof(constant_def_t, name) == 0, "name must come first");
static_assert(offsetof(pass_def_t, name) == 0, "name must come first");

/** Finds an entry by name. Works for every descriptor kind that has its name first. */
static const reg_entry_t *find_name(const reg_vec_t *v, const char *name, size_t len)
{
    for (size_t i = 0; i < v->len; i++) {
        const char *entry_name = *(const char *const *)v->data[i].def;
        if (name_eq(entry_name, name, len))
            return &v->data[i];
    }
    return NULL;
}

/** Shared part of the registry_add_* functions: checks the name, rejects duplicates, adds. */
static cas_status_t add_named(cas_t *cas, reg_vec_t *v, const void *def, const char *what, const char *name)
{
    if (!name || !*name)
        return cas_error(cas, CAS_ERR_REGISTRY, "%s needs a name", what);
    const reg_entry_t *dup = find_name(v, name, strlen(name));
    if (dup)
        return cas_error(cas, CAS_ERR_REGISTRY, "%s '%s' already registered by '%s'", what, name, dup->owner);
    cas_status_t st = vec_push(cas, v, def);
    if (st == CAS_OK)
        CAS_LOG(cas, LOG_DEBUG, "register %s '%s' (%s)", what, name, v->data[v->len - 1].owner);
    return st;
}

/** Registers a value type. */
cas_status_t registry_add_type(cas_t *cas, const value_type_t *t)
{
    return add_named(cas, &cas->reg.types, t, "value type", t->name);
}

/** Registers a function. */
cas_status_t registry_add_fn(cas_t *cas, const fn_def_t *fn)
{
    if (fn->min_args > fn->max_args)
        return cas_error(cas, CAS_ERR_REGISTRY, "function '%s' has min_args > max_args", fn->name);
    return add_named(cas, &cas->reg.fns, fn, "function", fn->name);
}

/** Registers a number syntax. */
cas_status_t registry_add_literal(cas_t *cas, const literal_def_t *lit)
{
    if (!lit->match || !lit->make)
        return cas_error(cas, CAS_ERR_REGISTRY, "literal '%s' needs match and make", lit->name);
    return add_named(cas, &cas->reg.literals, lit, "literal", lit->name);
}

/** Registers a constant. */
cas_status_t registry_add_constant(cas_t *cas, const constant_def_t *c)
{
    return add_named(cas, &cas->reg.constants, c, "constant", c->name);
}

/** Registers a pass. */
cas_status_t registry_add_pass(cas_t *cas, const pass_def_t *p)
{
    return add_named(cas, &cas->reg.passes, p, "pass", p->name);
}

/** Registers a display pass. */
cas_status_t registry_add_display_pass(cas_t *cas, const pass_def_t *p)
{
    return add_named(cas, &cas->reg.display_passes, p, "display pass", p->name);
}

/**
 * Registers an operator. Checks symbol shape, that the function takes the right
 * number of operands, and that (symbol, fixity) isn't taken yet.
 */
cas_status_t registry_add_op(cas_t *cas, const op_def_t *op)
{
    if (!op->symbol || !*op->symbol || !op->fn)
        return cas_error(cas, CAS_ERR_REGISTRY, "operator needs a symbol and a function");
    for (const char *c = op->symbol; *c; c++) {
        if (isspace((unsigned char)*c))
            return cas_error(cas, CAS_ERR_REGISTRY, "operator '%s' contains whitespace", op->symbol);
    }
    // Word symbols are lexed as identifiers, so they can't mix in punctuation.
    int word = isalpha((unsigned char)op->symbol[0]) || op->symbol[0] == '_';
    for (const char *c = op->symbol; *c; c++) {
        int is_word_char = isalnum((unsigned char)*c) || *c == '_';
        if (word != is_word_char)
            return cas_error(cas, CAS_ERR_REGISTRY, "operator '%s' mixes letters and symbols", op->symbol);
    }
    unsigned operands = op->fixity == FIX_INFIX ? 2 : 1;
    if (operands < op->fn->min_args || operands > op->fn->max_args)
        return cas_error(cas, CAS_ERR_REGISTRY, "operator '%s' needs a function taking %u argument(s)",
                         op->symbol, operands);
    if (op->fixity != FIX_INFIX && op->assoc != ASSOC_LEFT)
        return cas_error(cas, CAS_ERR_REGISTRY, "operator '%s': assoc only applies to infix", op->symbol);

    const reg_vec_t *v = &cas->reg.ops;
    for (size_t i = 0; i < v->len; i++) {
        const op_def_t *o = v->data[i].def;
        if (o->fixity == op->fixity && strcmp(o->symbol, op->symbol) == 0)
            return cas_error(cas, CAS_ERR_REGISTRY, "operator '%s' already registered by '%s'",
                             op->symbol, v->data[i].owner);
    }
    cas_status_t st = vec_push(cas, &cas->reg.ops, op);
    if (st == CAS_OK)
        CAS_LOG(cas, LOG_DEBUG, "register %s operator '%s' -> %s (%s)",
                op->fixity == FIX_PREFIX ? "prefix" : op->fixity == FIX_INFIX ? "infix" : "postfix",
                op->symbol, op->fn->name, cas->reg.ops.data[cas->reg.ops.len - 1].owner);
    return st;
}

/** Looks up a type by name. */
const value_type_t *registry_find_type(const cas_t *cas, const char *name)
{
    const reg_entry_t *e = find_name(&cas->reg.types, name, strlen(name));
    return e ? e->def : NULL;
}

/** Looks up a function by name. */
const fn_def_t *registry_find_fn(const cas_t *cas, const char *name, size_t len)
{
    const reg_entry_t *e = find_name(&cas->reg.fns, name, len);
    return e ? e->def : NULL;
}

/** Looks up a constant by name. */
const constant_def_t *registry_find_constant(const cas_t *cas, const char *name, size_t len)
{
    const reg_entry_t *e = find_name(&cas->reg.constants, name, len);
    return e ? e->def : NULL;
}

/** Looks up an operator by symbol and fixity. */
const op_def_t *registry_find_op(const cas_t *cas, const char *symbol, size_t len, fixity_t fixity)
{
    const reg_vec_t *v = &cas->reg.ops;
    for (size_t i = 0; i < v->len; i++) {
        const op_def_t *o = v->data[i].def;
        if (o->fixity == fixity && name_eq(o->symbol, symbol, len))
            return o;
    }
    return NULL;
}

/** Finds the operator with the given fixity that maps to fn. */
const op_def_t *registry_op_for_fn(const cas_t *cas, const fn_def_t *fn, fixity_t fixity)
{
    const reg_vec_t *v = &cas->reg.ops;
    for (size_t i = 0; i < v->len; i++) {
        const op_def_t *o = v->data[i].def;
        if (o->fn == fn && o->fixity == fixity)
            return o;
    }
    return NULL;
}

/** True for symbols like "mod" that read as identifiers. */
static int is_word_symbol(const char *s)
{
    return isalpha((unsigned char)s[0]) || s[0] == '_';
}

/** Longest symbolic operator that starts at s. */
const op_def_t *registry_match_op(const cas_t *cas, const char *s, size_t len)
{
    const op_def_t *best = NULL;
    size_t best_len = 0;
    const reg_vec_t *v = &cas->reg.ops;
    for (size_t i = 0; i < v->len; i++) {
        const op_def_t *o = v->data[i].def;
        size_t n = strlen(o->symbol);
        if (is_word_symbol(o->symbol) || n > len || n <= best_len)
            continue;
        if (memcmp(o->symbol, s, n) == 0) {
            best = o;
            best_len = n;
        }
    }
    return best;
}

/** True if any operator, of any fixity, uses this symbol. */
int registry_is_op_symbol(const cas_t *cas, const char *name, size_t len)
{
    const reg_vec_t *v = &cas->reg.ops;
    for (size_t i = 0; i < v->len; i++) {
        const op_def_t *o = v->data[i].def;
        if (name_eq(o->symbol, name, len))
            return 1;
    }
    return 0;
}

/** Asks every literal how much of s it matches and returns the longest. */
const literal_def_t *registry_match_literal(const cas_t *cas, const char *s, size_t len, size_t *out_len)
{
    const literal_def_t *best = NULL;
    size_t best_len = 0;
    const reg_vec_t *v = &cas->reg.literals;
    for (size_t i = 0; i < v->len; i++) {
        const literal_def_t *l = v->data[i].def;
        size_t n = l->match(s, len);
        if (n > best_len) {
            best = l;
            best_len = n;
        }
    }
    *out_len = best_len;
    return best;
}
