#include "core/bigrational.h"
#include "core/bignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * dst = gcd(|a|, |b|) with the binary (Stein) algorithm: only compares,
 * subtracts and halves, no division. Both x and y lose their factors of two at
 * the top of every iteration, not just the one that was swapped. Stripping
 * only one lets the other stay even for many rounds whenever it is the
 * smaller one, and the loop degrades into repeated subtraction (x=2,
 * y=~10^8 would take ~5*10^7 rounds instead of ~30).
 */
static void bignum_gcd(bignum_t *dst, const bignum_t *a, const bignum_t *b) {
    bignum_t x, y;
    bignum_init(&x, allocator_heap());
    bignum_init(&y, allocator_heap());
    bignum_copy(&x, a);
    bignum_copy(&y, b);

    if (x.sign < 0) bignum_negate(&x);
    if (y.sign < 0) bignum_negate(&y);

    if (bignum_is_zero(&x)) { bignum_copy(dst, &y); bignum_destroy(&x); bignum_destroy(&y); return; }
    if (bignum_is_zero(&y)) { bignum_copy(dst, &x); bignum_destroy(&x); bignum_destroy(&y); return; }

    uint64_t shift = 0;
    while (bignum_is_even(&x) && bignum_is_even(&y)) {
        bignum_shr1(&x, &x);
        bignum_shr1(&y, &y);
        shift++;
    }

    while (!bignum_is_zero(&y)) {
        while (bignum_is_even(&x)) bignum_shr1(&x, &x);
        while (bignum_is_even(&y)) bignum_shr1(&y, &y);
        if (bignum_cmp(&x, &y) > 0) {
            bignum_t tmp = x;
            x = y;
            y = tmp;
        }
        bignum_sub(&y, &y, &x);
    }

    if (shift > 0) {
        bignum_t two, pow2;
        bignum_init(&two, allocator_heap());
        bignum_init(&pow2, allocator_heap());
        bignum_from_i64(&two, 2);
        bignum_pow_u64(&pow2, &two, shift);
        bignum_mul(&x, &x, &pow2);
        bignum_destroy(&two);
        bignum_destroy(&pow2);
    }

    bignum_copy(dst, &x);

    bignum_destroy(&x);
    bignum_destroy(&y);
}

/** Puts q in lowest terms with a positive denominator. Zero becomes 0/1. */
static void reduce(bigrational_t *q) {
    if (bignum_is_zero(&q->num)) {
        bignum_from_i64(&q->denom, 1);
        return;
    }

    if (q->denom.sign < 0) {
        bignum_negate(&q->num);
        bignum_negate(&q->denom);
    }

    bignum_t g;
    bignum_init(&g, allocator_heap());
    bignum_gcd(&g, &q->num, &q->denom);

    if (bignum_cmp(&g, &bignum_one) != 0) {
        bignum_divmod(&q->num, NULL, &q->num, &g);
        bignum_divmod(&q->denom, NULL, &q->denom, &g);
    }

    bignum_destroy(&g);
}

/** Sets q to 0/1. Uses the heap, there is no allocator parameter. */
void bigrational_init(bigrational_t *q) {
    bignum_init(&q->num, allocator_heap());
    bignum_init(&q->denom, allocator_heap());
    bignum_from_i64(&q->denom, 1);
}

/** Frees both parts. */
void bigrational_destroy(bigrational_t *q) {
    bignum_destroy(&q->num);
    bignum_destroy(&q->denom);
}

/** Sets q to num/denom in lowest terms. Returns -1 if denom is zero. */
int bigrational_from_bignum(bigrational_t *q, const bignum_t *num, const bignum_t *denom) {
    if (bignum_is_zero(denom)) return -1;
    bignum_copy(&q->num, num);
    bignum_copy(&q->denom, denom);
    reduce(q);
    return 0;
}

/** Like bigrational_from_bignum for int64 arguments. */
int bigrational_from_int(bigrational_t *q, int64_t num, int64_t denom) {
    if (denom == 0) return -1;
    bignum_from_i64(&q->num, num);
    bignum_from_i64(&q->denom, denom);
    reduce(q);
    return 0;
}

/** Parses "n" or "n/d" in the given base. Returns -1 on bad input or a zero denominator. */
int bigrational_from_str(bigrational_t *q, const char *str, int base) {
    const char *slash = strchr(str, '/');
    if (!slash) {
        if (bignum_from_str(&q->num, str, strlen(str), base) != 0) return -1;
        bignum_from_i64(&q->denom, 1);
        reduce(q);
        return 0;
    }

    size_t num_len = (size_t)(slash - str);
    char *num_buf = malloc(num_len + 1);
    if (!num_buf) return -1;
    memcpy(num_buf, str, num_len);
    num_buf[num_len] = '\0';

    int rc = bignum_from_str(&q->num, num_buf, num_len, base);
    free(num_buf);
    if (rc != 0) return -1;

    if (bignum_from_str(&q->denom, slash + 1, strlen(slash + 1), base) != 0) return -1;

    if (bignum_is_zero(&q->denom)) return -1;

    reduce(q);
    return 0;
}

/** Prints "n/d", or just "n" if the denominator is 1. The caller frees the string. */
char *bigrational_to_str(const bigrational_t *q, int base) {
    char *num_str = bignum_to_str(&q->num, base);
    if (!num_str) return NULL;

    int is_integer = (bignum_cmp(&q->denom, &bignum_one) == 0);

    if (is_integer) return num_str;

    char *denom_str = bignum_to_str(&q->denom, base);
    if (!denom_str) {
        free(num_str);
        return NULL;
    }

    size_t len = strlen(num_str) + 1 + strlen(denom_str) + 1;
    char *out = malloc(len);
    if (!out) {
        free(num_str);
        return NULL;
    }
    snprintf(out, len, "%s/%s", num_str, denom_str);

    free(num_str);
    free(denom_str);
    return out;
}

/** True if q is zero. */
int bigrational_is_zero(const bigrational_t *q) {
    return bignum_is_zero(&q->num);
}

/** Compares by cross-multiplying. Returns -1, 0 or 1. */
int bigrational_cmp(const bigrational_t *a, const bigrational_t *b) {
    bignum_t lhs, rhs;
    bignum_init(&lhs, allocator_heap());
    bignum_init(&rhs, allocator_heap());
    bignum_mul(&lhs, &a->num, &b->denom);
    bignum_mul(&rhs, &b->num, &a->denom);

    int c = bignum_cmp(&lhs, &rhs);
    bignum_destroy(&lhs);
    bignum_destroy(&rhs);
    return c;
}

/** Flips the sign in place. */
void bigrational_negate(bigrational_t *q) {
    bignum_negate(&q->num);
}

/** Swaps numerator and denominator in place. Returns -1 if q is zero. */
int bigrational_invert(bigrational_t *q) {
    if (bignum_is_zero(&q->num)) return -1;
    bignum_t tmp;
    bignum_init(&tmp, allocator_heap());
    bignum_copy(&tmp, &q->num);
    bignum_copy(&q->num, &q->denom);
    bignum_copy(&q->denom, &tmp);
    bignum_destroy(&tmp);
    if (q->denom.sign < 0) {
        bignum_negate(&q->num);
        bignum_negate(&q->denom);
    }
    return 0;
}

/** dst = a + b, in lowest terms. */
void bigrational_add(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b) {
    bignum_t n1, n2;
    bignum_init(&n1, allocator_heap());
    bignum_init(&n2, allocator_heap());

    bignum_mul(&n1, &a->num, &b->denom);
    bignum_mul(&n2, &b->num, &a->denom);
    bignum_add(&dst->num, &n1, &n2);
    bignum_mul(&dst->denom, &a->denom, &b->denom);

    reduce(dst);

    bignum_destroy(&n1);
    bignum_destroy(&n2);
}

/** dst = a - b, in lowest terms. */
void bigrational_sub(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b) {
    bignum_t n1, n2;
    bignum_init(&n1, allocator_heap());
    bignum_init(&n2, allocator_heap());

    bignum_mul(&n1, &a->num, &b->denom);
    bignum_mul(&n2, &b->num, &a->denom);
    bignum_sub(&dst->num, &n1, &n2);
    bignum_mul(&dst->denom, &a->denom, &b->denom);

    reduce(dst);

    bignum_destroy(&n1);
    bignum_destroy(&n2);
}

/** dst = a * b, in lowest terms. */
void bigrational_mul(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b) {
    bignum_mul(&dst->num, &a->num, &b->num);
    bignum_mul(&dst->denom, &a->denom, &b->denom);
    reduce(dst);
}

/** dst = a / b, in lowest terms. Returns -1 if b is zero. */
int bigrational_div(bigrational_t *dst, const bigrational_t *a, const bigrational_t *b) {
    bignum_t denom;
    bignum_init(&denom, allocator_heap());

    bignum_mul(&denom, &b->num, &a->denom);

    if (bignum_is_zero(&denom)) {
        bignum_destroy(&denom);
        return -1;
    }

    bignum_mul(&dst->num, &a->num, &b->denom);
    bignum_copy(&dst->denom, &denom);
    reduce(dst);

    bignum_destroy(&denom);

    return 0;
}
