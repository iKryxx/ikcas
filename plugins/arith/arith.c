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
static const fn_def_t fn_pow;
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

/** Makes an int value from an int64_t. */
static cas_status_t int_value(cas_t *cas, value_t *out, int64_t v)
{
    bignum_t n;
    bignum_init(&n, allocator_heap());
    bignum_from_i64(&n, v);
    cas_status_t st = arith_make_int(cas, out, &n);
    bignum_destroy(&n);
    return st;
}

/** Builds a constant expr from int64_t */
static expr_t *int_const(cas_t *cas, span_t span, int64_t v) {
    value_t val;
    return int_value(cas, &val, v) == CAS_OK ? expr_new_const(cas, span, val) : NULL;
}

/** The constant node holding -c. NULL on error. */
static expr_t *negate_const(cas_t *cas, span_t span, const expr_t *c)
{
    value_t v;
    if (value_negate(cas, &v, &c->as.value) != CAS_OK)
        return NULL;
    return expr_new_const(cas, span, v);
}

/** The expression -b: a constant is negated directly, anything else becomes mul(-1, b). NULL on error. */
static expr_t *negated(cas_t *cas, span_t span, expr_t *b)
{
    if (expr_is_const(b))
        return negate_const(cas, span, b);
    expr_t *minus_one = int_const(cas, span, -1);
    if (!minus_one)
        return NULL;
    expr_t *factors[] = { minus_one, b };
    return expr_new_apply(cas, span, &fn_mul, factors, 2);
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
 * sub(a, b) -> add(a, -b), so later passes only deal with add and mul. See
 * negated for how -b is written.
 */
static expr_t *fn_sub_rewrite(cas_t *cas, expr_t *e)
{
    expr_t *neg_b = negated(cas, e->span, e->as.apply.args[1]);
    if (!neg_b)
        return NULL;
    expr_t *terms[] = { e->as.apply.args[0], neg_b };
    return expr_new_apply(cas, e->span, &fn_add, terms, 2);
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

/**
 * div(a, b) -> mul(a, b^-1), so later passes only deal with mul and pow.
 * A constant b is inverted right away with the numeric tower's own division.
 * That keeps the "division by zero" error, which pow(0, -1) would word
 * differently, and a float divisor is rounded once instead of twice. Two
 * constants are left for fold_constants, which divides them directly.
 */
static expr_t *fn_div_rewrite(cas_t *cas, expr_t *e)
{
    expr_t *a = e->as.apply.args[0];
    expr_t *b = e->as.apply.args[1];

    expr_t *inv_b;
    if (expr_is_const(b)) {
        if (expr_is_const(a))
            return e;
        value_t one, inverse;
        if (int_value(cas, &one, 1) != CAS_OK)
            return NULL;
        cas_status_t st = value_arith(cas, VOP_DIV, &inverse, &one, &b->as.value);
        value_destroy(&one);
        if (st != CAS_OK)
            return NULL;
        inv_b = expr_new_const(cas, e->span, inverse);
    } else {
        expr_t *minus_one = int_const(cas, e->span, -1);
        if (!minus_one)
            return NULL;
        expr_t *power[] = { b, minus_one };
        inv_b = expr_new_apply(cas, e->span, &fn_pow, power, 2);
    }
    if (!inv_b)
        return NULL;

    expr_t *factors[] = { a, inv_b };
    return expr_new_apply(cas, e->span, &fn_mul, factors, 2);
}

/** Reads the exponent's magnitude into out. Returns 0 if it doesn't fit in 64 bits. */
static int exponent_u64(const bignum_t *e, uint64_t *out)
{
    if (e->len > 2)
        return 0;
    uint64_t v = e->limb[0];
    if (e->len == 2)
        v |= (uint64_t)e->limb[1] << 32;
    *out = v;
    return 1;
}

/**
 * Exact base^e for an int or rational base. A negative exponent inverts the
 * base first, and zero has no inverse.
 */
static cas_status_t exact_pow(cas_t *cas, const value_t *base, uint64_t e, int negative, value_t *out)
{
    bignum_t num, den, rn, rd;
    bignum_init(&num, allocator_heap());
    bignum_init(&den, allocator_heap());
    bignum_init(&rn, allocator_heap());
    bignum_init(&rd, allocator_heap());

    bignum_copy(&num, arith_is_int(base) ? &base->as.i : &base->as.q.num);
    if (arith_is_int(base))
        bignum_from_i64(&den, 1);
    else
        bignum_copy(&den, &base->as.q.denom);

    cas_status_t st = CAS_OK;
    if (negative && e != 0) {
        if (bignum_is_zero(&num))
            st = cas_error(cas, CAS_ERR_DOMAIN, "zero to a negative power");
        else {
            bignum_t tmp = num;
            num = den;
            den = tmp;
        }
    }
    if (st == CAS_OK) {
        bignum_pow_u64(&rn, &num, e);
        bignum_pow_u64(&rd, &den, e);
        st = arith_make_rational(cas, out, &rn, &rd);
    }

    bignum_destroy(&num);
    bignum_destroy(&den);
    bignum_destroy(&rn);
    bignum_destroy(&rd);
    return st;
}

/** acc = acc * b through the numeric tower. acc stays valid if this fails. */
static cas_status_t mul_assign(cas_t *cas, value_t *acc, const value_t *b)
{
    value_t next;
    cas_status_t st = value_arith(cas, VOP_MUL, &next, acc, b);
    if (st == CAS_OK) {
        value_destroy(acc);
        *acc = next;
    }
    return st;
}

/**
 * base^e for a float base, by repeated squaring (at most 64 squarings). A
 * negative exponent divides 1 by the result, so a zero base gives inf like
 * 1/0.0 does. The core has no float power, and rounding happens at every
 * step, so the last digits can differ from a correctly rounded power.
 */
static cas_status_t float_pow(cas_t *cas, const value_t *base, uint64_t e, int negative, value_t *out)
{
    value_t one, result, square;
    cas_status_t st = int_value(cas, &one, 1);
    if (st != CAS_OK)
        return st;
    if (e == 0) {
        st = value_convert(cas, out, &one, &arith_type_float);
        value_destroy(&one);
        return st;
    }

    st = value_copy(cas, &square, base);
    if (st != CAS_OK) {
        value_destroy(&one);
        return st;
    }
    int have_result = 0;
    while (st == CAS_OK && e) {
        if (e & 1) {
            if (have_result) {
                st = mul_assign(cas, &result, &square);
            } else {
                st = value_copy(cas, &result, &square);
                have_result = st == CAS_OK;
            }
        }
        e >>= 1;
        if (st == CAS_OK && e)
            st = mul_assign(cas, &square, &square);
    }
    value_destroy(&square);

    if (st == CAS_OK && negative) {
        value_t inverse;
        st = value_arith(cas, VOP_DIV, &inverse, &one, &result);
        if (st == CAS_OK) {
            value_destroy(&result);
            result = inverse;
        }
    }
    value_destroy(&one);
    if (st == CAS_OK)
        *out = result;
    else if (have_result)
        value_destroy(&result);
    return st;
}

/**
 * pow(base, exp) for an int, rational or float base and an int exponent.
 * Int and rational bases stay exact, a float base gives a float.
 */
static cas_status_t fn_pow_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    const value_t *base = &args[0], *exp = &args[1];
    if (!arith_is_int(exp) || !(arith_is_int(base) || arith_is_rational(base) || arith_is_float(base)))
        return cas_error(cas, CAS_ERR_TYPE, "^ needs a number base and an int exponent");

    uint64_t e;
    if (!exponent_u64(&exp->as.i, &e))
        return cas_error(cas, CAS_ERR_DOMAIN, "exponent too large");

    int negative = exp->as.i.sign < 0;
    if (arith_is_float(base))
        return float_pow(cas, base, e, negative, out);
    return exact_pow(cas, base, e, negative, out);
}

/** neg(a). */
static cas_status_t fn_neg_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    return value_negate(cas, out, &args[0]);
}

/** neg(a) -> mul(-1, a), the same form sub and div use. */
static expr_t *fn_neg_rewrite(cas_t *cas, expr_t *e)
{
    return negated(cas, e->span, e->as.apply.args[0]);
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
static const fn_def_t fn_div = { "div", 2, 2, 0, fn_div_eval, fn_div_rewrite };
static const fn_def_t fn_neg = { "neg", 1, 1, 0, fn_neg_eval, fn_neg_rewrite };
static const fn_def_t fn_pow = { "pow", 2, 2, 0, fn_pow_eval, NULL };
static const fn_def_t fn_float = { "float", 1, 1, 0, fn_float_eval, NULL };

// display

/** True if e is the int constant -1. A float -1.0 is not, it decides the type of a product. */
static int is_minus_one(const expr_t *e)
{
    return expr_is_const(e) && arith_is_int(&e->as.value) && e->as.value.as.i.sign < 0 &&
           bignum_cmp_mag(&e->as.value.as.i, &bignum_one) == 0;
}

/**
 * True if e reads as a negative term: a negative constant, neg(x), or a
 * product that starts with one of those. The product pass runs first, so
 * -1*x has already become neg(x) by the time a sum looks at it.
 */
static int is_negative_term(const expr_t *e)
{
    if (expr_is_const(e))
        return arith_sign(&e->as.value) < 0;
    if (expr_is_apply_of(e, &fn_neg))
        return 1;
    if (!expr_is_apply_of(e, &fn_mul))
        return 0;
    const expr_t *first = e->as.apply.args[0];
    return expr_is_apply_of(first, &fn_neg) || (expr_is_const(first) && arith_sign(&first->as.value) < 0);
}

/**
 * The positive counterpart of a negative term: -3 gives 3, neg(x) gives x,
 * neg(x)*y gives x*y and -2*x gives 2*x. A float coefficient stays
 * (-1.0*x gives 1.0*x), since it decides the type of the result.
 */
static expr_t *flip_sign(cas_t *cas, span_t span, expr_t *e)
{
    if (expr_is_const(e))
        return negate_const(cas, span, e);
    if (expr_is_apply_of(e, &fn_neg))
        return e->as.apply.args[0];

    expr_t *const *args = e->as.apply.args;
    size_t n = e->as.apply.nargs;
    expr_t *first;
    if (expr_is_apply_of(args[0], &fn_neg))
        first = args[0]->as.apply.args[0];
    else if (!(first = negate_const(cas, span, args[0])))
        return NULL;

    expr_t **out = cas_scratch_alloc(cas, n * sizeof(*out));
    if (!out)
        return NULL;
    out[0] = first;
    for (size_t i = 1; i < n; i++)
        out[i] = args[i];
    return expr_new_apply(cas, span, &fn_mul, out, n);
}

/**
 * Turns add(a, b, -c, d) into add(sub(add(a, b), c), d). The printer writes a
 * left-leaning chain of equal-precedence operators without parentheses, so
 * this prints as a + b - c + d. Constants sort first, so a negative leading
 * term makes way for the first positive one (x - 3, not -3 + x); if every
 * term is negative the first one stays in front.
 */
static expr_t *display_sum(cas_t *cas, expr_t *e)
{
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

/**
 * Turns mul(-1, x, y) into mul(neg(x), y) and mul(-1, x) into neg(x), which
 * prints as -x * y and -x. Any other coefficient, a float -1.0 included, stays.
 */
static expr_t *display_product(cas_t *cas, expr_t *e)
{
    expr_t *const *args = e->as.apply.args;
    size_t n = e->as.apply.nargs;
    if (!is_minus_one(args[0]))
        return e;

    expr_t *first = expr_new_apply(cas, e->span, &fn_neg, &args[1], 1);
    if (!first || n == 2)
        return first;

    expr_t **out = cas_scratch_alloc(cas, (n - 1) * sizeof(*out));
    if (!out)
        return NULL;
    out[0] = first;
    for (size_t i = 2; i < n; i++)
        out[i - 1] = args[i];
    return expr_new_apply(cas, e->span, &fn_mul, out, n - 1);
}

/** Display callback: products first (expr_map is bottom-up), then the sums around them. */
static expr_t *display_node(cas_t *cas, expr_t *e, void *user)
{
    (void)user;
    if (expr_is_apply_of(e, &fn_mul))
        return display_product(cas, e);
    if (expr_is_apply_of(e, &fn_add))
        return display_sum(cas, e);
    return e;
}

/** Display pass entry point: shows negative terms as subtractions and a -1 factor as a minus sign. */
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
    // Binds tighter than prefix minus (30), so -2^2 is -(2^2).
    { "^", FIX_INFIX, 40, ASSOC_RIGHT, &fn_pow },
};

static const value_type_t *const types[] = { &arith_type_int, &arith_type_rational, &arith_type_float };
static const fn_def_t *const fns[] = { &fn_add, &fn_sub, &fn_mul, &fn_div, &fn_neg, &fn_pow, &fn_float };

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
