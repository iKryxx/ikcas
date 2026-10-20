#include "core/bigfloat.h"
#include "core/bignum_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/** Builds a heap bignum as a copy of the given limbs. Used to read a mantissa as an integer. */
static void bignum_from_limbs(bignum_t *n, const uint32_t *limbs, size_t len, int sign) {
    n->alloc = allocator_heap();
    n->limb = malloc(len * sizeof(uint32_t));
    if (!n->limb) abort(); // TODO: handle allocation failure
    memcpy(n->limb, limbs, len * sizeof(uint32_t));
    n->len = len;
    n->cap = len;
    n->sign = sign;
}

/** Sets n to 2^bits, on the heap. */
static void bignum_set_pow2(bignum_t *n, size_t bits) {
    size_t len = bits / 32 + 1;
    n->alloc = allocator_heap();
    n->limb = calloc(len, sizeof(uint32_t));
    if (!n->limb) abort(); // TODO: handle allocation failure
    n->limb[bits / 32] = 1u << (bits % 32);
    n->len = len;
    n->cap = len;
    n->sign = 1;
}

/** Grows the mantissa array to at least cap limbs. Aborts if the allocation fails. */
static void mant_ensure_cap(bigfloat_t *f, size_t cap) {
    if (f->mcap >= cap) return;
    size_t new_cap = f->mcap ? f->mcap * 2 : 4;
    if (new_cap < cap) new_cap = cap;
    uint32_t *p = realloc(f->mantissa, new_cap * sizeof(uint32_t));
    if (!p) abort(); // TODO: handle allocation failure
    f->mantissa = p;
    f->mcap = new_cap;
}

/** Index of the highest set bit of v, which must be nonzero. */
static int highest_set_bit(uint32_t v) {
    int pos = 31;
    while (!((v >> pos) & 1)) pos--;
    return pos;
}

/**
 * Shifts the limbs left by bits (below 32) in place. A carry out becomes a new top
 * limb if cap has room. Returns the new length.
 */
static size_t shl_small(uint32_t *m, size_t len, unsigned bits, size_t cap) {
    if (bits == 0) return len;
    uint32_t carry = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t v = m[i];
        m[i] = (v << bits) | carry;
        carry = v >> (32 - bits);
    }
    if (carry && len < cap) { m[len] = carry; len++; }
    return len;
}

/**
 * Rounds the mantissa to precision bits, to nearest with ties to even, and
 * shortens *mlen_io to match. Returns 1 if rounding carried out of the top
 * limb (the mantissa is then reset to top bit set, rest zero, and the caller must raise the
 * exponent by one).
 */
static int round_mantissa(uint32_t *m, size_t *mlen_io, size_t precision) {
    size_t mlen = *mlen_io;
    size_t total_bits = mlen * 32;

    if (precision >= total_bits) {
        return 0; // already fits
    }
    if (precision == 0) precision = 1;

    size_t keep_limbs = (precision + 31) / 32;
    size_t drop_bits = total_bits - keep_limbs * 32;
    size_t drop_limbs = drop_bits / 32;

    int round_bit = 0;
    int sticky = 0;
    if (drop_limbs > 0) {
        round_bit = (m[drop_limbs - 1] >> 31) & 1;
        if (m[drop_limbs - 1] & 0x7FFFFFFFu) sticky = 1;
        for (size_t i = 0; i + 1 < drop_limbs && !sticky; i++) if (m[i]) sticky = 1;
    }

    memmove(m, m + drop_limbs, keep_limbs * sizeof(uint32_t));
    mlen = keep_limbs;

    int round_up;
    if (!round_bit) {
        round_up = 0;
    } else if (sticky) {
        round_up = 1;
    } else {
        round_up = (m[0] & 1) ? 1 : 0;
    }

    int carried_out = 0;
    if (round_up) {
        uint64_t carry = 1;
        for (size_t i = 0; i < mlen && carry; i++) {
            uint64_t v = (uint64_t)m[i] + carry;
            m[i] = (uint32_t)v;
            carry = v >> 32;
        }
        if (carry) {
            memset(m, 0, mlen * sizeof(uint32_t));
            m[mlen - 1] = 0x80000000u;
            carried_out = 1;
        }
    }

    *mlen_io = mlen;
    return carried_out;
}

/**
 * Drops zero limbs and shifts the mantissa left until the top bit is set,
 * lowering the exponent to match. A zero mantissa turns the value into zero.
 */
static void normalize_finite(bigfloat_t *f) {
    while (f->mlen > 1 && f->mantissa[f->mlen - 1] == 0) f->mlen--;

    if (f->mlen == 1 && f->mantissa[0] == 0) {
        f->kind = BIGFLOAT_ZERO;
        return;
    }

    uint32_t top = f->mantissa[f->mlen - 1];
    int top_bit = highest_set_bit(top);
    int shift = 31 - top_bit;
    if (shift > 0) {
        mant_ensure_cap(f, f->mlen + 1);
        f->mlen = shl_small(f->mantissa, f->mlen, (unsigned)shift, f->mcap);
        f->exponent -= shift;
        while (f->mlen > 1 && f->mantissa[f->mlen - 1] == 0) f->mlen--;
    }
}

/** Sets f to zero with the given precision (in bits, at least 1). */
void bigfloat_init(bigfloat_t *f, size_t precision_bits) {
    f->kind = BIGFLOAT_ZERO;
    f->sign = 1;
    f->mantissa = NULL;
    f->mlen = 1;
    f->mcap = 0;
    f->exponent = 0;
    f->precision = precision_bits ? precision_bits : 1;
    mant_ensure_cap(f, (f->precision + 31) / 32 + 1);
    f->mantissa[0] = 0;
}

/** Frees the mantissa. */
void bigfloat_free(bigfloat_t *f) {
    free(f->mantissa);
    f->mantissa = NULL;
    f->mlen = 0;
    f->mcap = 0;
}

/** dst = src, rounded to dst's precision. dst has to be initialized. */
void bigfloat_copy(bigfloat_t *dst, const bigfloat_t *src) {
    dst->kind = src->kind;
    dst->sign = src->sign;
    dst->exponent = src->exponent;
    mant_ensure_cap(dst, src->mlen);
    memcpy(dst->mantissa, src->mantissa, src->mlen * sizeof(uint32_t));
    dst->mlen = src->mlen;
    if (dst->kind == BIGFLOAT_FINITE) {
        int carry = round_mantissa(dst->mantissa, &dst->mlen, dst->precision);
        if (carry) { dst->exponent += 1; }
    }
}

/** Makes f a zero with the given sign. */
void bigfloat_set_zero(bigfloat_t *f, int sign) {
    f->kind = BIGFLOAT_ZERO;
    f->sign = sign < 0 ? -1 : 1;
    f->mantissa[0] = 0;
    f->mlen = 1;
    f->exponent = 0;
}

/** Makes f an infinity with the given sign. */
void bigfloat_set_inf(bigfloat_t *f, int sign) {
    f->kind = BIGFLOAT_INF;
    f->sign = sign < 0 ? -1 : 1;
}

/** Makes f a NaN. */
void bigfloat_set_nan(bigfloat_t *f) {
    f->kind = BIGFLOAT_NAN;
    f->sign = 1;
}

/** True for zero, either sign. */
int bigfloat_is_zero(const bigfloat_t *f) { return f->kind == BIGFLOAT_ZERO; }

/** True for either infinity. */
int bigfloat_is_inf(const bigfloat_t *f)  { return f->kind == BIGFLOAT_INF; }

/** True for NaN. */
int bigfloat_is_nan(const bigfloat_t *f)  { return f->kind == BIGFLOAT_NAN; }

/** Flips the sign in place. NaN is left alone. */
void bigfloat_negate(bigfloat_t *f) {
    if (f->kind == BIGFLOAT_NAN) return;
    f->sign = -f->sign;
}

/** Sets f to n, rounded to f's precision. */
void bigfloat_from_bignum(bigfloat_t *f, const bignum_t *n) {
    if (bignum_is_zero(n)) { bigfloat_set_zero(f, 1); return; }

    f->kind = BIGFLOAT_FINITE;
    f->sign = n->sign;
    mant_ensure_cap(f, n->len);
    memcpy(f->mantissa, n->limb, n->len * sizeof(uint32_t));
    f->mlen = n->len;
    // The value is stored as 0.mantissa * 2^exponent, so for an integer the
    // exponent starts as the bit length of the limbs and normalize_finite trims it.
    f->exponent = (int64_t)n->len * 32;

    normalize_finite(f);
    if (f->kind == BIGFLOAT_FINITE) {
        int carry = round_mantissa(f->mantissa, &f->mlen, f->precision);
        if (carry) f->exponent += 1;
    }
}

/** Sets f to v, rounded to f's precision. */
void bigfloat_from_i64(bigfloat_t *f, int64_t v) {
    bignum_t n;
    bignum_init(&n, allocator_heap());
    bignum_from_i64(&n, v);
    bigfloat_from_bignum(f, &n);
    bignum_destroy(&n);
}

/**
 * Sets f to num/denom. Scales the numerator by 2^(precision + 64) and does an
 * integer division, so the quotient has more bits than rounding needs. A zero
 * denominator gives NaN.
 */
void bigfloat_from_ratio(bigfloat_t *f, const bignum_t *num, const bignum_t *denom) {
    if (bignum_is_zero(denom)) { bigfloat_set_nan(f); return; }
    if (bignum_is_zero(num)) { bigfloat_set_zero(f, 1); return; }

    size_t guard_bits = 64;
    size_t scale_bits = f->precision + guard_bits;

    bignum_t scale, scaled_num, q, r, denom_mag;
    bignum_set_pow2(&scale, scale_bits);
    bignum_init(&scaled_num, allocator_heap()); bignum_init(&q, allocator_heap()); bignum_init(&r, allocator_heap()); bignum_init(&denom_mag, allocator_heap());

    bignum_mul(&scaled_num, num, &scale);
    if (scaled_num.sign < 0) bignum_negate(&scaled_num);
    bignum_copy(&denom_mag, denom);
    if (denom_mag.sign < 0) bignum_negate(&denom_mag);

    bignum_divmod(&q, &r, &scaled_num, &denom_mag);

    bigfloat_from_bignum(f, &q);
    f->exponent -= (int64_t)scale_bits;
    f->sign = num->sign * denom->sign;

    bignum_destroy(&scale); bignum_destroy(&scaled_num); bignum_destroy(&q);
    bignum_destroy(&r); bignum_destroy(&denom_mag);
}

/**
 * Parses decimal text like "-12.5e-3" exactly as a ratio of integers, then
 * rounds it to the given precision. Returns -1 on bad input.
 */
int bigfloat_from_str(bigfloat_t *f, const char *str, size_t precision_bits) {
    int sign = 1;
    const char *p = str;
    if (*p == '+' || *p == '-') { if (*p == '-') sign = -1; p++; }
    if (*p == '\0') return -1;

    const char *dot = strchr(p, '.');
    const char *exp_marker = strchr(p, 'e');
    if (!exp_marker) exp_marker = strchr(p, 'E');

    size_t int_len = dot ? (size_t)(dot - p) : (exp_marker ? (size_t)(exp_marker - p) : strlen(p));
    size_t frac_start = dot ? (size_t)(dot - p) + 1 : 0;
    size_t frac_len = 0;
    if (dot) {
        const char *frac_end = exp_marker ? exp_marker : p + strlen(p);
        frac_len = (size_t)(frac_end - (dot + 1));
    }

    long decimal_exp = 0;
    if (exp_marker) decimal_exp = strtol(exp_marker + 1, NULL, 10);

    size_t digits_len = int_len + frac_len;
    if (digits_len == 0) return -1;
    char *digits = malloc(digits_len + 1);
    if (!digits) return -1;
    memcpy(digits, p, int_len);
    if (dot) memcpy(digits + int_len, p + frac_start, frac_len);
    digits[digits_len] = '\0';

    for (size_t i = 0; i < digits_len; i++) if (!isdigit((unsigned char)digits[i])) { free(digits); return -1; }

    bignum_t num, denom, ten, pow_adjust;
    bignum_init(&num, allocator_heap()); bignum_init(&denom, allocator_heap()); bignum_init(&ten, allocator_heap()); bignum_init(&pow_adjust, allocator_heap());
    if (bignum_from_str(&num, digits, digits_len, 10) != 0) { free(digits); bignum_destroy(&num); bignum_destroy(&denom); bignum_destroy(&ten); bignum_destroy(&pow_adjust); return -1; }
    free(digits);
    num.sign = sign;

    bignum_from_i64(&ten, 10);
    bignum_pow_u64(&denom, &ten, (uint64_t)frac_len);

    long abs_exp = decimal_exp < 0 ? -decimal_exp : decimal_exp;
    bignum_pow_u64(&pow_adjust, &ten, (uint64_t)abs_exp);
    if (decimal_exp >= 0) {
        bignum_mul(&num, &num, &pow_adjust);
    } else {
        bignum_mul(&denom, &denom, &pow_adjust);
    }

    f->precision = precision_bits ? precision_bits : 1;
    bigfloat_from_ratio(f, &num, &denom);

    bignum_destroy(&num); bignum_destroy(&denom); bignum_destroy(&ten); bignum_destroy(&pow_adjust);
    return 0;
}

/**
 * Prints the exact binary value in decimal. decimal_digits caps the digits after
 * the point (the rest is cut off, not rounded). Zero or less means
 * precision / 3 + 2. The caller frees the string.
 */
char *bigfloat_to_str(const bigfloat_t *f, int decimal_digits) {
    if (f->kind == BIGFLOAT_NAN) { char *s = malloc(4); strcpy(s, "nan"); return s; }
    if (f->kind == BIGFLOAT_INF) { char *s = malloc(5); strcpy(s, f->sign < 0 ? "-inf" : "inf"); return s; }
    if (f->kind == BIGFLOAT_ZERO) { char *s = malloc(2); strcpy(s, "0"); return s; }

    bignum_t mant_int;
    bignum_from_limbs(&mant_int, f->mantissa, f->mlen, 1);

    int64_t bit_exp = f->exponent - (int64_t)(f->mlen * 32);

    bignum_t num, denom, pow2;
    bignum_init(&num, allocator_heap()); bignum_init(&denom, allocator_heap());
    bignum_copy(&num, &mant_int);
    bignum_from_i64(&denom, 1);
    bignum_set_pow2(&pow2, (size_t)(bit_exp >= 0 ? bit_exp : -bit_exp));
    if (bit_exp >= 0) {
        bignum_mul(&num, &num, &pow2);
    } else {
        bignum_mul(&denom, &denom, &pow2);
    }

    bignum_t int_part, rem, ten;
    bignum_init(&int_part, allocator_heap()); bignum_init(&rem, allocator_heap()); bignum_init(&ten, allocator_heap());
    bignum_from_i64(&ten, 10);
    bignum_divmod(&int_part, &rem, &num, &denom);

    char *int_str = bignum_to_str(&int_part, 10);

    int max_frac = decimal_digits > 0 ? decimal_digits : (int)(f->precision / 3) + 2;
    char *frac_digits = malloc((size_t)max_frac + 1);
    bignum_t digit_q;
    bignum_init(&digit_q, allocator_heap());
    int fi = 0;
    for (; fi < max_frac; fi++) {
        if (bignum_is_zero(&rem)) break;
        bignum_mul(&rem, &rem, &ten);
        bignum_divmod(&digit_q, &rem, &rem, &denom);
        frac_digits[fi] = (char)('0' + digit_q.limb[0]);
    }
    bignum_destroy(&digit_q);
    frac_digits[fi] = '\0';

    size_t sign_len = (f->sign < 0) ? 1 : 0;
    size_t total_len = sign_len + strlen(int_str) + 1 + (size_t)fi + 1 + 1;
    char *out = malloc(total_len);
    const char *sign_prefix = (f->sign < 0) ? "-" : "";
    if (fi > 0) snprintf(out, total_len, "%s%s.%s", sign_prefix, int_str, frac_digits);
    else snprintf(out, total_len, "%s%s", sign_prefix, int_str);

    free(int_str);
    free(frac_digits);
    bignum_destroy(&mant_int); bignum_destroy(&num); bignum_destroy(&denom); bignum_destroy(&pow2);
    bignum_destroy(&int_part); bignum_destroy(&rem); bignum_destroy(&ten);
    return out;
}

/**
 * Shifts the limb array left by shift bits in place, growing it when needed.
 * Used to line up two mantissas on a common exponent.
 */
static void extend_shifted(uint32_t **m, size_t *len, size_t *cap, size_t shift) {
    size_t limb_shift = shift / 32;
    unsigned bit_shift = shift % 32;
    size_t new_len = *len + limb_shift + 1;
    if (*cap < new_len) {
        uint32_t *p = realloc(*m, new_len * sizeof(uint32_t));
        if (!p) abort(); // TODO: handle allocation failure
        *m = p;
        *cap = new_len;
    }

    memmove(*m + limb_shift, *m, *len * sizeof(uint32_t));
    memset(*m, 0, limb_shift * sizeof(uint32_t));
    size_t total = *len + limb_shift;
    if (bit_shift) total = shl_small(*m, total, bit_shift, new_len);
    *len = total;
}

/**
 * Shared code for add and sub (neg flips b). Handles NaN, infinity and zero
 * first, then aligns both mantissas to the lower exponent, adds or subtracts
 * the magnitudes, and rounds.
 */
static void add_impl(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b, int neg) {
    int b_sign_eff = neg ? -b->sign : b->sign;

    if (a->kind == BIGFLOAT_NAN || b->kind == BIGFLOAT_NAN) { bigfloat_set_nan(dst); return; }
    if (a->kind == BIGFLOAT_INF && b->kind == BIGFLOAT_INF) {
        if (a->sign == b_sign_eff) { bigfloat_set_inf(dst, a->sign); return; }
        bigfloat_set_nan(dst); return;
    }
    if (a->kind == BIGFLOAT_INF) { bigfloat_set_inf(dst, a->sign); return; }
    if (b->kind == BIGFLOAT_INF) { bigfloat_set_inf(dst, b_sign_eff); return; }
    if (a->kind == BIGFLOAT_ZERO && b->kind == BIGFLOAT_ZERO) {
        bigfloat_set_zero(dst, (a->sign == b_sign_eff) ? a->sign : 1); return;
    }
    if (a->kind == BIGFLOAT_ZERO) {
        bigfloat_copy(dst, b);
        if (neg) bigfloat_negate(dst);
        return;
    }
    if (b->kind == BIGFLOAT_ZERO) { bigfloat_copy(dst, a); return; }

    size_t abits = a->mlen * 32, bbits = b->mlen * 32;
    int64_t a_lsb_exp = a->exponent - (int64_t)abits;
    int64_t b_lsb_exp = b->exponent - (int64_t)bbits;

    uint32_t *am = malloc((a->mlen + 2) * sizeof(uint32_t));
    uint32_t *bm = malloc((b->mlen + 2) * sizeof(uint32_t));
    size_t alen = a->mlen, blen = b->mlen, acap = a->mlen + 2, bcap = b->mlen + 2;
    memcpy(am, a->mantissa, a->mlen * sizeof(uint32_t));
    memcpy(bm, b->mantissa, b->mlen * sizeof(uint32_t));

    int64_t common_lsb_exp = a_lsb_exp < b_lsb_exp ? a_lsb_exp : b_lsb_exp;
    extend_shifted(&am, &alen, &acap, (size_t)(a_lsb_exp - common_lsb_exp));
    extend_shifted(&bm, &blen, &bcap, (size_t)(b_lsb_exp - common_lsb_exp));

    size_t rlen = (alen > blen ? alen : blen) + 1;
    uint32_t *r = calloc(rlen, sizeof(uint32_t));

    int result_sign;
    size_t result_len;
    if (a->sign == b_sign_eff) {
        result_len = add_mag(r, am, alen, bm, blen);
        result_sign = a->sign;
    } else {
        int cmp = cmp_mag_raw(am, alen, bm, blen);
        const uint32_t *big = cmp >= 0 ? am : bm;
        const uint32_t *small = cmp >= 0 ? bm : am;
        size_t biglen = cmp >= 0 ? alen : blen;
        size_t smalllen = cmp >= 0 ? blen : alen;
        result_len = sub_mag(r, big, biglen, small, smalllen);
        result_sign = (cmp >= 0) ? a->sign : b_sign_eff;
    }

    free(am); free(bm);

    while (result_len > 1 && r[result_len - 1] == 0) result_len--;
    if (result_len == 1 && r[0] == 0) {
        bigfloat_set_zero(dst, 1);
        free(r);
        return;
    }

    dst->kind = BIGFLOAT_FINITE;
    dst->sign = result_sign;
    mant_ensure_cap(dst, result_len);
    memcpy(dst->mantissa, r, result_len * sizeof(uint32_t));
    dst->mlen = result_len;
    dst->exponent = common_lsb_exp + (int64_t)(result_len * 32);
    free(r);

    normalize_finite(dst);
    if (dst->kind == BIGFLOAT_FINITE) {
        int carry = round_mantissa(dst->mantissa, &dst->mlen, dst->precision);
        if (carry) dst->exponent += 1;
    }
}

/** dst = a + b. */
void bigfloat_add(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b) { add_impl(dst, a, b, 0); }

/** dst = a - b. */
void bigfloat_sub(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b) { add_impl(dst, a, b, 1); }

/** dst = a * b. Multiplies the mantissas, adds the exponents, rounds. */
void bigfloat_mul(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b) {
    if (a->kind == BIGFLOAT_NAN || b->kind == BIGFLOAT_NAN) { bigfloat_set_nan(dst); return; }
    int rsign = a->sign * b->sign;
    if ((a->kind == BIGFLOAT_INF && b->kind == BIGFLOAT_ZERO) ||
        (a->kind == BIGFLOAT_ZERO && b->kind == BIGFLOAT_INF)) { bigfloat_set_nan(dst); return; }
    if (a->kind == BIGFLOAT_INF || b->kind == BIGFLOAT_INF) { bigfloat_set_inf(dst, rsign); return; }
    if (a->kind == BIGFLOAT_ZERO || b->kind == BIGFLOAT_ZERO) { bigfloat_set_zero(dst, rsign); return; }


    size_t rlen = a->mlen + b->mlen;
    uint32_t *acc = malloc(rlen * sizeof(uint32_t));
    mul_mag(acc, a->mantissa, a->mlen, b->mantissa, b->mlen);

    dst->kind = BIGFLOAT_FINITE;
    dst->sign = rsign;
    mant_ensure_cap(dst, rlen);
    memcpy(dst->mantissa, acc, rlen * sizeof(uint32_t));
    dst->mlen = rlen;
    dst->exponent = a->exponent + b->exponent;
    free(acc);

    normalize_finite(dst);
    if (dst->kind == BIGFLOAT_FINITE) {
        int carry = round_mantissa(dst->mantissa, &dst->mlen, dst->precision);
        if (carry) dst->exponent += 1;
    }
}

/**
 * dst = a / b. Special values follow IEEE (0/0 and inf/inf are NaN, x/0 is
 * inf). Otherwise it divides the mantissas as integers via bigfloat_from_ratio
 * and fixes up the exponent.
 */
void bigfloat_div(bigfloat_t *dst, const bigfloat_t *a, const bigfloat_t *b) {
    if (a->kind == BIGFLOAT_NAN || b->kind == BIGFLOAT_NAN) { bigfloat_set_nan(dst); return; }
    int rsign = a->sign * b->sign;
    if (a->kind == BIGFLOAT_INF && b->kind == BIGFLOAT_INF) { bigfloat_set_nan(dst); return; }
    if (a->kind == BIGFLOAT_ZERO && b->kind == BIGFLOAT_ZERO) { bigfloat_set_nan(dst); return; }
    if (b->kind == BIGFLOAT_ZERO) { bigfloat_set_inf(dst, rsign); return; }
    if (a->kind == BIGFLOAT_INF) { bigfloat_set_inf(dst, rsign); return; }
    if (b->kind == BIGFLOAT_INF) { bigfloat_set_zero(dst, rsign); return; }
    if (a->kind == BIGFLOAT_ZERO) { bigfloat_set_zero(dst, rsign); return; }


    bignum_t a_int, b_int;
    bignum_from_limbs(&a_int, a->mantissa, a->mlen, 1);
    bignum_from_limbs(&b_int, b->mantissa, b->mlen, 1);


    int64_t exp_diff = (a->exponent - (int64_t)(a->mlen * 32)) - (b->exponent - (int64_t)(b->mlen * 32));

    bigfloat_from_ratio(dst, &a_int, &b_int);
    dst->exponent += exp_diff;
    dst->sign = rsign;

    bignum_destroy(&a_int); bignum_destroy(&b_int);
}

/** Returns -1, 0 or 1, or 2 if either value is NaN. */
int bigfloat_cmp(const bigfloat_t *a, const bigfloat_t *b) {
    if (a->kind == BIGFLOAT_NAN || b->kind == BIGFLOAT_NAN) return 2;

    if (a->kind == BIGFLOAT_ZERO && b->kind == BIGFLOAT_ZERO) return 0;
    if (a->kind == BIGFLOAT_ZERO) return b->sign > 0 ? -1 : 1;
    if (b->kind == BIGFLOAT_ZERO) return a->sign > 0 ? 1 : -1;

    if (a->kind == BIGFLOAT_INF || b->kind == BIGFLOAT_INF) {
        if (a->kind == BIGFLOAT_INF && b->kind == BIGFLOAT_INF) return a->sign == b->sign ? 0 : (a->sign > b->sign ? 1 : -1);
        if (a->kind == BIGFLOAT_INF) return a->sign > 0 ? 1 : -1;
        return b->sign > 0 ? -1 : 1;
    }

    if (a->sign != b->sign) return a->sign > b->sign ? 1 : -1;


    int mag_cmp;
    if (a->exponent != b->exponent) {
        mag_cmp = a->exponent < b->exponent ? -1 : 1;
    } else {
        mag_cmp = 0;
        size_t maxlen = a->mlen > b->mlen ? a->mlen : b->mlen;
        for (size_t k = 0; k < maxlen && mag_cmp == 0; k++) {
            uint32_t av = k < a->mlen ? a->mantissa[a->mlen - 1 - k] : 0;
            uint32_t bv = k < b->mlen ? b->mantissa[b->mlen - 1 - k] : 0;
            if (av != bv) mag_cmp = av < bv ? -1 : 1;
        }
    }
    return a->sign > 0 ? mag_cmp : -mag_cmp;
}
