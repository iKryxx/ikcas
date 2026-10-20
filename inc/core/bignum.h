#ifndef BIGNUM_H
#define BIGNUM_H

#include "misc/allocator.h"

#include <stdint.h>
#include <stddef.h>

/** Arbitrary-size integer: sign and magnitude, 32-bit limbs. */
typedef struct {
    uint32_t *limb; // little-endian
    size_t len;
    size_t cap;
    int sign;
    allocator_t alloc; // owns limb, zeroed = heap
} bignum_t;

/** The constant 1. */
extern const bignum_t bignum_one;

/** Sets n to zero. All later allocations for it go through alloc. */
void bignum_init(bignum_t *n, allocator_t alloc);

/** Frees the limbs. n can't be used until it's initialized again. */
void bignum_destroy(bignum_t *n);

/** dst = src. dst has to be initialized. */
void bignum_copy(bignum_t *dst, const bignum_t *src);

/** Sets n from the 64 bits of v read as unsigned (the parameter type is int64_t). */
void bignum_from_u64(bignum_t *n, int64_t v);

/** Sets n to v. */
void bignum_from_i64(bignum_t *n, int64_t v);

/**
 * Parses len chars of str in base 2 to 36, with optional leading signs. Returns
 * -1 on bad input.
 */
int bignum_from_str(bignum_t *n, const char *str, size_t len, int base);

/** bignum_from_str for a NUL-terminated string. */
int bignum_from_cstr(bignum_t *n, const char *str, int base);

/** Prints in base 2 to 36 as a malloc'd string. NULL on a bad base or when out of memory. */
char *bignum_to_str(const bignum_t *n, int base);

/** Flips the sign in place. Zero stays positive. */
void bignum_negate(bignum_t *n);

/** dst = a + b. dst may be a or b. */
void bignum_add(bignum_t *dst, const bignum_t *a, const bignum_t *b);

/** dst = a - b. dst may be a or b. */
void bignum_sub(bignum_t *dst, const bignum_t *a, const bignum_t *b);

/** dst = a * b. dst may be a or b. */
void bignum_mul(bignum_t *dst, const bignum_t *a, const bignum_t *b);

/** dst = base^exp. dst may be base. */
void bignum_pow_u64(bignum_t *dst, const bignum_t *base, uint64_t exp);

/**
 * q = a / b and r = a % b, truncated toward zero, r takes a's sign. Either can be
 * NULL. They may alias a or b. b must not be zero.
 */
void bignum_divmod(bignum_t *q, bignum_t *r, const bignum_t *a, const bignum_t *b);

/** True if n is zero. */
int bignum_is_zero(const bignum_t *n);

/** True if n is even. */
int bignum_is_even(const bignum_t *n);

/** Compares signed values. Returns -1, 0 or 1. */
int bignum_cmp(const bignum_t *a, const bignum_t *b);

/** Compares absolute values. Returns -1, 0 or 1. */
int bignum_cmp_mag(const bignum_t *a, const bignum_t *b);

/** dst = n / 2, rounded toward zero. dst may be n. */
void bignum_shr1(bignum_t *dst, const bignum_t *n);

#endif // BIGNUM_H
