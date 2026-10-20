#ifndef BIGRATIONAL_H
#define BIGRATIONAL_H

#include "core/bignum.h"

/** A fraction in lowest terms. The denominator is always positive. */
typedef struct {
    bignum_t num;
    bignum_t denom;
} bigrational_t;

/** Sets q to 0/1. */
void bigrational_init(bigrational_t *q);

/** Frees both parts. */
void bigrational_destroy(bigrational_t *q);

/** dst = src. Declared but not implemented, don't use it. */
void bigrational_copy(bigrational_t *dst, const bigrational_t *src);

/** Sets q to num/denom, reduced. Returns -1 if denom is zero. */
int bigrational_from_bignum(bigrational_t *q, const bignum_t *num, const bignum_t *denom);

/** Sets q to num/denom, reduced. Returns -1 if denom is zero. */
int bigrational_from_int(bigrational_t *q, int64_t num, int64_t denom);

/** Parses "n" or "n/d". Returns -1 on bad input or a zero denominator. */
int bigrational_from_str(bigrational_t *q, const char *str, int base);

/** Prints "n/d", or "n" if the denominator is 1. Malloc'd, the caller frees it. */
char *bigrational_to_str(const bigrational_t *q, int base);

/** dst = a + b. */
void bigrational_add(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b);

/** dst = a - b. */
void bigrational_sub(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b);

/** dst = a * b. */
void bigrational_mul(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b);

/** dst = a / b. Returns -1 if b is zero. */
int bigrational_div(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b);

/** Returns -1, 0 or 1. */
int bigrational_cmp(const bigrational_t *a, const bigrational_t *b);

/** True if q is zero. */
int bigrational_is_zero(const bigrational_t *q);

/** Flips the sign in place. */
void bigrational_negate(bigrational_t *q);

/** Replaces q with 1/q in place. Returns -1 if q is zero. */
int bigrational_invert(bigrational_t *q);

#endif // BIGRATIONAL_H
