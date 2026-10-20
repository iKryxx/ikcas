// Arbitrary precision binary floating point.
//
// value = sign * 0.mantissa * 2^exponent. The mantissa is kept normalized:
// the top bit of the top limb is set (except for zero, inf and nan). The
// exponent counts bits. Each value has its own precision, the number of
// mantissa bits kept, and every operation rounds its result to the
// destination's precision.
//
// Special values follow IEEE 754 instead of being errors: 0*inf and inf-inf
// give nan, x/0 gives inf. Rounding is to nearest, ties to even (see
// round_mantissa in bigfloat.c).

#ifndef BIGFLOAT_H
#define BIGFLOAT_H

#include "bignum.h"
#include <stdint.h>

/** What a bigfloat holds. */
typedef enum {
    BIGFLOAT_ZERO,
    BIGFLOAT_FINITE,
    BIGFLOAT_INF,
    BIGFLOAT_NAN
} bigfloat_kind_t;

/** A floating point number. Fields are internal, use the functions below. */
typedef struct {
    bigfloat_kind_t kind;
    int       sign;
    uint32_t *mantissa;
    size_t    mlen;
    size_t    mcap;
    int64_t   exponent;
    size_t    precision;
} bigfloat_t;

/** Sets f to zero. precision_bits is the mantissa size and must be at least 1. */
void bigfloat_init(bigfloat_t *f, size_t precision_bits);

/** Frees the mantissa. */
void bigfloat_free(bigfloat_t *f);

/** dst = src, rounded to dst's precision. dst has to be initialized. */
void bigfloat_copy(bigfloat_t *dst, const bigfloat_t *src);

/** Makes f a zero with the given sign. */
void bigfloat_set_zero(bigfloat_t *f, int sign);

/** Makes f an infinity with the given sign. */
void bigfloat_set_inf(bigfloat_t *f, int sign);

/** Makes f a NaN. */
void bigfloat_set_nan(bigfloat_t *f);

/** Sets f to v, rounded to f's precision. */
void bigfloat_from_i64(bigfloat_t *f, int64_t v);

/** Sets f to n, rounded to f's precision. */
void bigfloat_from_bignum(bigfloat_t *f, const bignum_t *n);

/** Sets f to num/denom, rounded to f's precision. A zero denominator gives NaN. */
void bigfloat_from_ratio(bigfloat_t *f, const bignum_t *num, const bignum_t *denom);

/** Parses decimal text such as "-12.5e-3" at the given precision. Returns -1 on bad input. */
int  bigfloat_from_str(bigfloat_t *f, const char *str, size_t precision_bits);

/**
 * Prints in decimal, with at most decimal_digits digits after the point (zero or
 * less picks a default from the precision). The caller frees the string.
 */
char *bigfloat_to_str(const bigfloat_t *f, int decimal_digits);

/** dst = a + b. */
void bigfloat_add(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b);

/** dst = a - b. */
void bigfloat_sub(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b);

/** dst = a * b. */
void bigfloat_mul(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b);

/** dst = a / b. */
void bigfloat_div(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b);

/** Returns -1, 0 or 1, or 2 if either value is NaN. */
int bigfloat_cmp(const bigfloat_t *a, const bigfloat_t *b);

/** True for zero. */
int bigfloat_is_zero(const bigfloat_t *f);

/** True for either infinity. */
int bigfloat_is_inf(const bigfloat_t *f);

/** True for NaN. */
int bigfloat_is_nan(const bigfloat_t *f);

/** Flips the sign in place. */
void bigfloat_negate(bigfloat_t *f);

#endif // BIGFLOAT_H
