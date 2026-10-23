#include "arith.h"
#include "core/bignum.h"
#include "engine/cas.h"
#include "expr/expr.h"
#include "expr/op.h"
#include "expr/value.h"
#include "misc/allocator.h"

#include <ctype.h>

// forward declarations
static const fn_def_t fn_add;
static const fn_def_t fn_sub;
static const fn_def_t fn_mul;
static const fn_def_t fn_div;
static const fn_def_t fn_neg;
static const fn_def_t fn_float;

// integer literals

/** Matches a run of decimal digits. */
static size_t int_literal_match(const char *s, size_t len)
{
    size_t n = 0;
    while (n < len && isdigit((unsigned char)s[n]))
        n++;
    return n;
}

/** Builds an int from decimal digits. */
static cas_status_t int_literal_make(cas_t *cas, const char *text, size_t len, value_t *out)
{
    out->type = &arith_type_int;
    bignum_init(&out->as.i, allocator_heap());
    if (bignum_from_str(&out->as.i, text, len, 10) != 0) {
        bignum_destroy(&out->as.i);
        return cas_error(cas, CAS_ERR_SYNTAX, "invalid number '%.*s'", (int)len, text);
    }
    return CAS_OK;
}

static const literal_def_t int_literal = { "int", int_literal_match, int_literal_make };

/** Builds a constant expr from int64_t */
static expr_t *int_const(cas_t *cas, span_t span, int64_t v) {
    bignum_t v_int;
    bignum_init(&v_int, allocator_heap());
    bignum_from_i64(&v_int, v);
    value_t v_val;
    cas_status_t st = arith_make_int(cas, &v_val, &v_int);
    bignum_destroy(&v_int);

    return st == CAS_OK ? expr_new_const(cas, span, v_val) : NULL;
}

// functions

/**
 * Applies op left to right over all args, so variadic add and mul behave like
 * a chain of the binary operator.
 */
static cas_status_t fold_arith(cas_t *cas, value_op_t op, const value_t *args, size_t n, value_t *out)
{
    value_t acc;
    cas_status_t st = value_copy(cas, &acc, &args[0]);
    for (size_t i = 1; st == CAS_OK && i < n; i++) {
        value_t next;
        st = value_arith(cas, op, &next, &acc, &args[i]);
        value_destroy(&acc);
        if (st == CAS_OK)
            acc = next;
    }
    if (st == CAS_OK)
        *out = acc;
    return st;
}

/** add(a, b, ...). */
static cas_status_t fn_add_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_ADD, args, n, out);
}

/** sub(a, b). */
static cas_status_t fn_sub_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_SUB, args, n, out);
}

/**
 * sub(a, b) -> add(a, -b), the negation folded for a constant b and written as
 * mul(-1, b) otherwise, so later passes only deal with add and mul.
 */
static expr_t *fn_sub_rewrite(cas_t *cas, expr_t *e) {
    expr_t *a = e->as.apply.args[0];
    expr_t *b = e->as.apply.args[1];

    expr_t *neg_b;
    if (expr_is_const(b)) {
        value_t v;
        if(value_negate(cas, &v, &b->as.value) != CAS_OK)
            return NULL;
        neg_b = expr_new_const(cas, e->span, v);
    } else {
        expr_t *neg_one_expr = int_const(cas, e->span, -1);
        if (neg_one_expr == NULL)
            return NULL;
        expr_t* args[] = { neg_one_expr, b };
        neg_b = expr_new_apply(cas, e->span, &fn_mul, args, 2);
        if (neg_b == NULL)
            return NULL;
    }

    return expr_new_apply(cas, e->span, &fn_add, (expr_t*[]){a, neg_b}, 2);
}

/** mul(a, b, ...). */
static cas_status_t fn_mul_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_MUL, args, n, out);
}

/** div(a, b). */
static cas_status_t fn_div_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_DIV, args, n, out);
}

/** neg(a). */
static cas_status_t fn_neg_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    return value_negate(cas, out, &args[0]);
}

/** float(a): converts to a float. */
static cas_status_t fn_float_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    return value_convert(cas, out, &args[0], &arith_type_float);
}

static const fn_def_t fn_add = { "add", 2, FN_VARIADIC, FN_COMMUTATIVE | FN_ASSOCIATIVE, fn_add_eval, NULL };
static const fn_def_t fn_sub = { "sub", 2, 2, 0, fn_sub_eval, fn_sub_rewrite };
static const fn_def_t fn_mul = { "mul", 2, FN_VARIADIC, FN_COMMUTATIVE | FN_ASSOCIATIVE, fn_mul_eval, NULL };
static const fn_def_t fn_div = { "div", 2, 2, 0, fn_div_eval, NULL };
static const fn_def_t fn_neg = { "neg", 1, 1, 0, fn_neg_eval, NULL };
static const fn_def_t fn_float = { "float", 1, 1, 0, fn_float_eval, NULL };

// display

/** The constant node holding -c. NULL on error. */
static expr_t *negate_const(cas_t *cas, span_t span, const expr_t *c)
{
    value_t v;
    if (value_negate(cas, &v, &c->as.value) != CAS_OK)
        return NULL;
    return expr_new_const(cas, span, v);
}

/** True if e is a negative constant or a product that starts with one (the canonical form of -b). */
static int is_negative_term(const expr_t *e)
{
    if (expr_is_const(e))
        return arith_sign(&e->as.value) < 0;
    return expr_is_apply_of(e, &fn_mul) && expr_is_const(e->as.apply.args[0]) &&
           arith_sign(&e->as.apply.args[0]->as.value) < 0;
}

/**
 * The positive counterpart of a negative term: -3 gives 3, -2*x gives 2*x and
 * -1*x gives x. A float coefficient stays (-1.0*x gives 1.0*x), since it
 * decides the type of the result.
 */
static expr_t *flip_sign(cas_t *cas, span_t span, expr_t *e)
{
    if (expr_is_const(e))
        return negate_const(cas, span, e);

    expr_t *const *args = e->as.apply.args;
    size_t n = e->as.apply.nargs;
    expr_t *coef = negate_const(cas, span, args[0]);
    if (!coef)
        return NULL;

    int unit = arith_is_int(&coef->as.value) && bignum_cmp(&coef->as.value.as.i, &bignum_one) == 0;
    if (unit && n == 2)
        return args[1];

    expr_t **out = cas_scratch_alloc(cas, n * sizeof(*out));
    if (!out)
        return NULL;
    size_t m = 0;
    if (!unit)
        out[m++] = coef;
    for (size_t i = 1; i < n; i++)
        out[m++] = args[i];
    return expr_new_apply(cas, span, &fn_mul, out, m);
}

/**
 * Turns add(a, b, -c, d) into add(sub(add(a, b), c), d). The printer writes a
 * left-leaning chain of equal-precedence operators without parentheses, so
 * this prints as a + b - c + d. Constants sort first, so a negative leading
 * term makes way for the first positive one (x - 3, not -3 + x); if every
 * term is negative the first one stays in front.
 */
static expr_t *display_node(cas_t *cas, expr_t *e, void *user)
{
    (void)user;
    if (!expr_is_apply_of(e, &fn_add))
        return e;

    expr_t *const *args = e->as.apply.args;
    size_t n = e->as.apply.nargs;
    size_t lead = 0; // index of the term that goes first
    if (is_negative_term(args[0])) {
        while (lead < n && is_negative_term(args[lead]))
            lead++;
        if (lead == n)
            lead = 0;
    }

    expr_t **terms = cas_scratch_alloc(cas, n * sizeof(*terms)); // display order
    expr_t **pending = cas_scratch_alloc(cas, n * sizeof(*pending)); // terms not yet in a sub
    if (!terms || !pending)
        return NULL;
    size_t nt = 0, negatives = 0;
    terms[nt++] = args[lead];
    for (size_t i = 0; i < n; i++) {
        if (i == lead)
            continue;
        negatives += is_negative_term(args[i]);
        terms[nt++] = args[i];
    }
    if (!negatives)
        return e;

    size_t np = 0;
    pending[np++] = terms[0];
    for (size_t i = 1; i < n; i++) {
        if (!is_negative_term(terms[i])) {
            pending[np++] = terms[i];
            continue;
        }
        expr_t *left = np == 1 ? pending[0] : expr_new_apply(cas, e->span, &fn_add, pending, np);
        expr_t *pos = left ? flip_sign(cas, e->span, terms[i]) : NULL;
        if (!pos)
            return NULL;
        expr_t *operands[] = { left, pos };
        pending[0] = expr_new_apply(cas, e->span, &fn_sub, operands, 2);
        if (!pending[0])
            return NULL;
        np = 1;
    }
    return np == 1 ? pending[0] : expr_new_apply(cas, e->span, &fn_add, pending, np);
}

/** Display pass entry point: rewrites sums with negative terms into subtractions. */
static expr_t *arith_display(cas_t *cas, expr_t *root)
{
    return expr_map(cas, root, display_node, NULL);
}

static const pass_def_t pass_arith_display = { "arith_display", arith_display };

// syntax

static const op_def_t ops[] = {
    { "+", FIX_INFIX, 10, ASSOC_LEFT, &fn_add },
    { "-", FIX_INFIX, 10, ASSOC_LEFT, &fn_sub },
    { "*", FIX_INFIX, 20, ASSOC_LEFT, &fn_mul },
    { "/", FIX_INFIX, 20, ASSOC_LEFT, &fn_div },
    { "-", FIX_PREFIX, 30, ASSOC_LEFT, &fn_neg },
};

static const value_type_t *const types[] = { &arith_type_int, &arith_type_rational, &arith_type_float };
static const fn_def_t *const fns[] = { &fn_add, &fn_sub, &fn_mul, &fn_div, &fn_neg, &fn_float };

/** Registers the numeric types, the integer literal, the functions and their operators. */
static cas_status_t arith_init(cas_t *cas)
{
    cas_status_t st = CAS_OK;
    for (size_t i = 0; st == CAS_OK && i < sizeof(types) / sizeof(*types); i++)
        st = registry_add_type(cas, types[i]);
    if (st == CAS_OK)
        st = registry_add_literal(cas, &int_literal);
    for (size_t i = 0; st == CAS_OK && i < sizeof(fns) / sizeof(*fns); i++)
        st = registry_add_fn(cas, fns[i]);
    for (size_t i = 0; st == CAS_OK && i < sizeof(ops) / sizeof(*ops); i++)
        st = registry_add_op(cas, &ops[i]);
    if (st == CAS_OK)
        st = registry_add_display_pass(cas, &pass_arith_display);
    return st;
}

IKCAS_PLUGIN(arith, "1.0", arith_init, NULL);
