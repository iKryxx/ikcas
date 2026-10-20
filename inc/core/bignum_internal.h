#ifndef BIGNUM_INTERNAL_H
#define BIGNUM_INTERNAL_H

// Raw limb primitives shared by bignum.c and bigfloat.c. Not public API,
// don't include this from anywhere else.

#include <stdint.h>
#include <stddef.h>

/** Compares two magnitudes by length, then limb by limb. Returns -1, 0 or 1. */
int cmp_mag_raw(const uint32_t *a, size_t alen, const uint32_t *b, size_t blen);

/** dst = a + b. dst needs max(alen, blen) + 1 limbs. Returns the result length. */
size_t add_mag(uint32_t *dst, const uint32_t *a, size_t alen, const uint32_t *b, size_t blen);

/** dst = a - b, with a >= b and alen >= blen. dst needs alen limbs. Returns the trimmed length. */
size_t sub_mag(uint32_t *dst, const uint32_t *a, size_t alen, const uint32_t *b, size_t blen);

/** dst = a * b. dst needs exactly an + bn limbs and gets zeroed first. */
void mul_mag(uint32_t *dst, const uint32_t *al, size_t an, const uint32_t *bl, size_t bn);

#endif // BIGNUM_INTERNAL_H
