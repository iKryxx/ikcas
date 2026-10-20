#include "arith.h"
#include "engine/cas.h"

#include <stdlib.h>

// The numeric tower. Kernels return the simplest type that holds the result,
// so 6/3 is the int 2 and 1/2 + 1/2 is the int 1.

static const allocator_t heap = { NULL, NULL };

/**
 * Moves q into dst, as an int if the denominator is 1, otherwise as a rational.
 * q can't be used afterwards.
 */
static void finish_rational(value_t *dst, bigrational_t *q)
{
    if (bignum_cmp(&q->denom, &bignum_one) == 0) {
        dst->type = &arith_type_int;
        dst->as.i = q->num;
        bignum_destroy(&q->denom);
    } else {
        dst->type = &arith_type_rational;
        dst->as.q = *q;
    }
}

/** Reports a division by zero. */
static cas_status_t div_by_zero(cas_t *cas)
{
    return cas_error(cas, CAS_ERR_DOMAIN, "division by zero");
}

// int

/** Frees an int. */
static void int_destroy(value_t *v)
{
    bignum_destroy(&v->as.i);
}

/** Copies an int. */
static cas_status_t int_copy(cas_t *cas, value_t *dst, const value_t *src)
{
    (void)cas;
    dst->type = &arith_type_int;
    bignum_init(&dst->as.i, heap);
    bignum_copy(&dst->as.i, &src->as.i);
    return CAS_OK;
}

/** Decimal text of an int. */
static char *int_to_str(const value_t *v)
{
    return bignum_to_str(&v->as.i, 10);
}

/** Orders two ints. */
static int int_cmp(const value_t *a, const value_t *b)
{
    return bignum_cmp(&a->as.i, &b->as.i);
}

/** Runs a bignum operation into a fresh int. */
static cas_status_t int_arith(value_t *dst, const value_t *a, const value_t *b,
                              void (*op)(bignum_t *, const bignum_t *, const bignum_t *))
{
    dst->type = &arith_type_int;
    bignum_init(&dst->as.i, heap);
    op(&dst->as.i, &a->as.i, &b->as.i);
    return CAS_OK;
}

/** int + int. */
static cas_status_t int_add(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return int_arith(dst, a, b, bignum_add);
}

/** int - int. */
static cas_status_t int_sub(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return int_arith(dst, a, b, bignum_sub);
}

/** int * int. */
static cas_status_t int_mul(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return int_arith(dst, a, b, bignum_mul);
}

/** int / int. Exact, so the result may be a rational. */
static cas_status_t int_div(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    bigrational_t q;
    bigrational_init(&q);
    if (bigrational_from_bignum(&q, &a->as.i, &b->as.i) != 0) {
        bigrational_destroy(&q);
        return div_by_zero(cas);
    }
    finish_rational(dst, &q);
    return CAS_OK;
}

/** -int. */
static cas_status_t int_neg(cas_t *cas, value_t *dst, const value_t *a)
{
    int_copy(cas, dst, a);
    bignum_negate(&dst->as.i);
    return CAS_OK;
}

const value_type_t arith_type_int = {
    .name = "int",
    .rank = 10,
    .destroy = int_destroy,
    .copy = int_copy,
    .to_str = int_to_str,
    .cmp = int_cmp,
    .binop = { [VOP_ADD] = int_add, [VOP_SUB] = int_sub, [VOP_MUL] = int_mul, [VOP_DIV] = int_div },
    .neg = int_neg,
};

// rational

/** Frees a rational. */
static void rat_destroy(value_t *v)
{
    bigrational_destroy(&v->as.q);
}

/** Copies a rational. */
static cas_status_t rat_copy(cas_t *cas, value_t *dst, const value_t *src)
{
    (void)cas;
    dst->type = &arith_type_rational;
    // bigrational_copy is declared in core/bigrational.h but never defined
    bigrational_init(&dst->as.q);
    bignum_copy(&dst->as.q.num, &src->as.q.num);
    bignum_copy(&dst->as.q.denom, &src->as.q.denom);
    return CAS_OK;
}

/** Turns an int into a rational with denominator 1. */
static cas_status_t rat_promote_from(cas_t *cas, value_t *dst, const value_t *src)
{
    if (src->type != &arith_type_int)
        return cas_error(cas, CAS_ERR_TYPE, "cannot convert %s to rational", src->type->name);
    dst->type = &arith_type_rational;
    bigrational_init(&dst->as.q); // denominator starts at 1
    bignum_copy(&dst->as.q.num, &src->as.i);
    return CAS_OK;
}

/** Text of a rational: "n/d", or just "n" when d is 1. */
static char *rat_to_str(const value_t *v)
{
    return bigrational_to_str(&v->as.q, 10);
}

/** Orders two rationals. */
static int rat_cmp(const value_t *a, const value_t *b)
{
    return bigrational_cmp(&a->as.q, &b->as.q);
}

/** Runs a bigrational operation into a fresh value, collapsing to int if it can. */
static cas_status_t rat_arith(value_t *dst, const value_t *a, const value_t *b,
                              void (*op)(bigrational_t *, const bigrational_t *, const bigrational_t *))
{
    bigrational_t r;
    bigrational_init(&r);
    op(&r, &a->as.q, &b->as.q);
    finish_rational(dst, &r);
    return CAS_OK;
}

/** rational + rational. */
static cas_status_t rat_add(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return rat_arith(dst, a, b, bigrational_add);
}

/** rational - rational. */
static cas_status_t rat_sub(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return rat_arith(dst, a, b, bigrational_sub);
}

/** rational * rational. */
static cas_status_t rat_mul(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return rat_arith(dst, a, b, bigrational_mul);
}

/** rational / rational. Division by zero is an error. */
static cas_status_t rat_div(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    bigrational_t r;
    bigrational_init(&r);
    if (bigrational_div(&r, &a->as.q, &b->as.q) != 0) {
        bigrational_destroy(&r);
        return div_by_zero(cas);
    }
    finish_rational(dst, &r);
    return CAS_OK;
}

/** -rational. */
static cas_status_t rat_neg(cas_t *cas, value_t *dst, const value_t *a)
{
    rat_copy(cas, dst, a);
    bigrational_negate(&dst->as.q);
    return CAS_OK;
}

const value_type_t arith_type_rational = {
    .name = "rational",
    .rank = 20,
    .destroy = rat_destroy,
    .copy = rat_copy,
    .to_str = rat_to_str,
    .promote_from = rat_promote_from,
    .cmp = rat_cmp,
    .binop = { [VOP_ADD] = rat_add, [VOP_SUB] = rat_sub, [VOP_MUL] = rat_mul, [VOP_DIV] = rat_div },
    .neg = rat_neg,
};

// float

/** Frees a float. */
static void float_destroy(value_t *v)
{
    bigfloat_free(&v->as.f);
}

/** Copies a float, keeping its precision. */
static cas_status_t float_copy(cas_t *cas, value_t *dst, const value_t *src)
{
    (void)cas;
    dst->type = &arith_type_float;
    bigfloat_init(&dst->as.f, src->as.f.precision);
    bigfloat_copy(&dst->as.f, &src->as.f);
    return CAS_OK;
}

/** Converts an int or rational to a float at the session's precision. */
static cas_status_t float_promote_from(cas_t *cas, value_t *dst, const value_t *src)
{
    dst->type = &arith_type_float;
    bigfloat_init(&dst->as.f, cas->opts.float_bits);
    if (src->type == &arith_type_int) {
        bigfloat_from_bignum(&dst->as.f, &src->as.i);
    } else if (src->type == &arith_type_rational) {
        bigfloat_from_ratio(&dst->as.f, &src->as.q.num, &src->as.q.denom);
    } else {
        bigfloat_free(&dst->as.f);
        return cas_error(cas, CAS_ERR_TYPE, "cannot convert %s to float", src->type->name);
    }
    return CAS_OK;
}

/** Decimal text of a float, with as many digits as its precision carries. */
static char *float_to_str(const value_t *v)
{
    // precision is in bits, and a bit is about 0.30103 decimal digits
    int digits = (int)(v->as.f.precision * 30103 / 100000);
    return bigfloat_to_str(&v->as.f, digits > 0 ? digits : 1);
}

/** Orders two floats. */
static int float_cmp(const value_t *a, const value_t *b)
{
    return bigfloat_cmp(&a->as.f, &b->as.f);
}

/** Runs a bigfloat operation at the higher of the two precisions. */
static cas_status_t float_arith(value_t *dst, const value_t *a, const value_t *b,
                                void (*op)(bigfloat_t *, const bigfloat_t *, const bigfloat_t *))
{
    size_t bits = a->as.f.precision > b->as.f.precision ? a->as.f.precision : b->as.f.precision;
    dst->type = &arith_type_float;
    bigfloat_init(&dst->as.f, bits);
    op(&dst->as.f, &a->as.f, &b->as.f);
    return CAS_OK;
}

/** float + float. */
static cas_status_t float_add(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return float_arith(dst, a, b, bigfloat_add);
}

/** float - float. */
static cas_status_t float_sub(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return float_arith(dst, a, b, bigfloat_sub);
}

/** float * float. */
static cas_status_t float_mul(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return float_arith(dst, a, b, bigfloat_mul);
}

/** float / float. Follows IEEE rules, so 1.0/0.0 is inf. */
static cas_status_t float_div(cas_t *cas, value_t *dst, const value_t *a, const value_t *b)
{
    (void)cas;
    return float_arith(dst, a, b, bigfloat_div);
}

/** -float. */
static cas_status_t float_neg(cas_t *cas, value_t *dst, const value_t *a)
{
    float_copy(cas, dst, a);
    bigfloat_negate(&dst->as.f);
    return CAS_OK;
}

const value_type_t arith_type_float = {
    .name = "float",
    .rank = 30,
    .destroy = float_destroy,
    .copy = float_copy,
    .to_str = float_to_str,
    .promote_from = float_promote_from,
    .cmp = float_cmp,
    .binop = { [VOP_ADD] = float_add, [VOP_SUB] = float_sub, [VOP_MUL] = float_mul, [VOP_DIV] = float_div },
    .neg = float_neg,
};

// helpers for other plugins

/** True if v is an int. */
int arith_is_int(const value_t *v)
{
    return v->type == &arith_type_int;
}

/** True if v is a rational. */
int arith_is_rational(const value_t *v)
{
    return v->type == &arith_type_rational;
}

/** True if v is a float. */
int arith_is_float(const value_t *v)
{
    return v->type == &arith_type_float;
}

/** -1, 0 or 1. Returns 0 for values that aren't arith types. */
int arith_sign(const value_t *v)
{
    if (arith_is_int(v))
        return bignum_is_zero(&v->as.i) ? 0 : v->as.i.sign;
    if (arith_is_rational(v))
        return bignum_is_zero(&v->as.q.num) ? 0 : v->as.q.num.sign;
    if (arith_is_float(v))
        return bigfloat_is_zero(&v->as.f) ? 0 : v->as.f.sign;
    return 0;
}

/** Makes an int from a copy of n. */
cas_status_t arith_make_int(cas_t *cas, value_t *out, const bignum_t *n)
{
    value_t tmp = { .type = &arith_type_int, .as.i = *n };
    return int_copy(cas, out, &tmp);
}

/** Makes num/den in lowest terms, as an int if it divides evenly. */
cas_status_t arith_make_rational(cas_t *cas, value_t *out, const bignum_t *num, const bignum_t *den)
{
    bigrational_t q;
    bigrational_init(&q);
    if (bigrational_from_bignum(&q, num, den) != 0) {
        bigrational_destroy(&q);
        return div_by_zero(cas);
    }
    finish_rational(out, &q);
    return CAS_OK;
}
