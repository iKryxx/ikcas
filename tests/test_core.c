// Tests for the numeric core (bignum, bigrational, bigfloat, arena): known
// values, random cross-checks against __int128, and benchmarks.
//
// usage:
//   ikcas_test          correctness suite (what `make test` runs)
//   ikcas_test bench    benchmarks only
//   ikcas_test all      both
#include "core/bignum.h"
#include "core/bigrational.h"
#include "core/bigfloat.h"
#include "misc/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

static int failures = 0;

/** Compares a bignum's decimal text with the expected one and counts a failure if they differ. */
static void check_str(const char *label, const bignum_t *n, const char *expect) {
    char *s = bignum_to_str(n, 10);
    if (strcmp(s, expect) != 0) {
        printf("FAIL %s: got %s, expected %s\n", label, s, expect);
        failures++;
    } else {
        printf("PASS %s\n", label);
    }
    free(s);
}

/** Like check_str, in the given base. */
static void check_str_base(const char *label, const bignum_t *n, int base, const char *expect) {
    char *s = bignum_to_str(n, base);
    if (strcmp(s, expect) != 0) {
        printf("FAIL %s: got %s, expected %s\n", label, s, expect);
        failures++;
    } else {
        printf("Pass %s\n", label);
    }
    free(s);
}

/** Compares a rational's text with the expected one. */
static void check_rat_str(const char *label, const bigrational_t *q, int base, const char *expect) {
    char *s = bigrational_to_str(q, base);
    if (strcmp(s, expect) != 0) {
        printf("FAIL %s: got %s, expected %s\n", label, s, expect);
        failures++;
    } else {
        printf("PASS %s\n", label);
    }
    free(s);
}

/** Compares a float's text (at most decimal_digits after the point) with the expected one. */
static void check_bigfloat_str(const char *label, const bigfloat_t *f, int decimal_digits, const char *expect) {
    char *s = bigfloat_to_str(f, decimal_digits);
    if (strcmp(s, expect) != 0) {
        printf("FAIL %s: got %s, expected %s\n", label, s, expect);
        failures++;
    } else {
        printf("PASS %s\n", label);
    }
    free(s);
}

/**
 * Random decimal string of the given length with a nonzero first digit. Makes
 * multi-limb operands (more than KARATSUBA_THRESHOLD limbs) without writing
 * huge literals. The caller frees it.
 */
static char *random_decimal_string(int digits) {
    char *s = malloc((size_t)digits + 1);
    s[0] = (char)('1' + rand() % 9);
    for (int i = 1; i < digits; i++) s[i] = (char)('0' + rand() % 10);
    s[digits] = '\0';
    return s;
}

/** Small values, INT64_MIN and a long number survive a trip through text. */
static void test_basic_roundtrip(void) {
    bignum_t n;
    bignum_init(&n, allocator_heap());
    bignum_from_cstr(&n, "123456789012345678901234567890", 10);
    check_str("roundtrip big", &n, "123456789012345678901234567890");
    bignum_destroy(&n);

    bignum_init(&n, allocator_heap());
    bignum_from_cstr(&n, "-987654321", 10);
    check_str("roundtrip neg", &n, "-987654321");
    bignum_destroy(&n);

    bignum_init(&n, allocator_heap());
    bignum_from_cstr(&n, "0", 10);
    check_str("roundtrip zero", &n, "0");
    bignum_destroy(&n);

    bignum_init(&n, allocator_heap());
    bignum_from_i64(&n, -9223372036854775807LL - 1); // INT64_MIN
    check_str("int64 min", &n, "-9223372036854775808");
    bignum_destroy(&n);

    // A long number exercises the growth path of ensure_cap.
    bignum_init(&n, allocator_heap());
    char *big = random_decimal_string(2000);
    bignum_from_cstr(&n, big, 10);
    check_str("long number roundtrip", &n, big);
    free(big);
    bignum_destroy(&n);
}

/** Signs, bad characters, empty input. */
static void test_string_parsing(void) {
    bignum_t n;
    bignum_init(&n, allocator_heap());

    if (bignum_from_cstr(&n, "123", 1) != -1) { printf("FAIL base too small accepted\n"); failures++; }
    if (bignum_from_cstr(&n, "123", 37) != -1) { printf("FAIL base too large accepted\n"); failures++; }
    if (bignum_from_cstr(&n, "12a", 10) != -1) { printf("FAIL invalid digit accepted\n"); failures++; }
    if (bignum_from_cstr(&n, "", 10) != -1) { printf("FAIL empty string accepted\n"); failures++; }
    if (bignum_from_cstr(&n, "-", 10) != -1) { printf("FAIL bare sign accepted\n"); failures++; }

    bignum_from_cstr(&n, "+123", 10);
    check_str("leading plus", &n, "123");

    // Several leading signs are fine, each '-' flips the sign.
    bignum_from_cstr(&n, "+-1", 10);
    check_str("mixed leading signs", &n, "-1");
    bignum_from_cstr(&n, "--5", 10);
    check_str("double negative sign", &n, "5");

    bignum_from_cstr(&n, "-0", 10);
    check_str("negative zero normalizes sign", &n, "0");

    bignum_from_cstr(&n, "007", 10);
    check_str("leading zeros", &n, "7");

    bignum_destroy(&n);
}

/** Printing and parsing in bases other than 10. */
static void test_bases(void) {
    bignum_t n;
    bignum_init(&n, allocator_heap());

    bignum_from_cstr(&n, "ff", 16);
    check_str_base("hex roundtrip", &n, 16, "ff");
    check_str("hex to decimal", &n, "255");

    bignum_from_cstr(&n, "z", 36);
    check_str_base("base36 roundtrip", &n, 36, "z");
    check_str("base36 to decimal", &n, "35");

    bignum_from_cstr(&n, "-1010", 2);
    check_str_base("binary roundtrip", &n, 2, "-1010");
    check_str("binary to decimal", &n, "-10");

    bignum_destroy(&n);
}

/** Addition and subtraction across signs, carries and zero. */
static void test_add_sub(void) {
    bignum_t a, b, r;
    bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap()); bignum_init(&r, allocator_heap());

    bignum_from_cstr(&a, "999999999999999999999999999999", 10);
    bignum_from_cstr(&b, "1", 10);
    bignum_add(&r, &a, &b);
    check_str("add carry chain", &r, "1000000000000000000000000000000");

    bignum_from_cstr(&a, "5", 10);
    bignum_from_cstr(&b, "8", 10);
    bignum_sub(&r, &a, &b);
    check_str("sub goes negative", &r, "-3");

    bignum_from_cstr(&a, "-5", 10);
    bignum_from_cstr(&b, "-8", 10);
    bignum_add(&r, &a, &b);
    check_str("neg + neg", &r, "-13");

    bignum_from_cstr(&a, "-5", 10);
    bignum_from_cstr(&b, "8", 10);
    bignum_add(&r, &a, &b);
    check_str("neg + pos", &r, "3");

    // a - a is 0 with the positive sign.
    bignum_from_cstr(&a, "-42", 10);
    bignum_sub(&r, &a, &a);
    check_str("self subtraction", &r, "0");
    if (r.sign != 1) { printf("FAIL self subtraction sign: got %d, expected 1\n", r.sign); failures++; }

    // Subtracting from or with zero changes nothing or just negates.
    bignum_from_cstr(&a, "123", 10);
    bignum_from_cstr(&b, "0", 10);
    bignum_sub(&r, &a, &b);
    check_str("sub zero", &r, "123");
    bignum_sub(&r, &b, &a);
    check_str("zero minus", &r, "-123");

    bignum_destroy(&a); bignum_destroy(&b); bignum_destroy(&r);
}

/** Multiplication with known results, including multi-limb carries. */
static void test_mul(void) {
    bignum_t a, b, r;
    bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap()); bignum_init(&r, allocator_heap());

    bignum_from_cstr(&a, "123456789", 10);
    bignum_from_cstr(&b, "987654321", 10);
    bignum_mul(&r, &a, &b);
    check_str("mul known", &r, "121932631112635269");

    bignum_from_cstr(&a, "0", 10);
    bignum_from_cstr(&b, "999999999999999999", 10);
    bignum_mul(&r, &a, &b);
    check_str("mul by zero", &r, "0");

    bignum_from_cstr(&a, "-12", 10);
    bignum_from_cstr(&b, "34", 10);
    bignum_mul(&r, &a, &b);
    check_str("mul sign", &r, "-408");

    // 2^128 * 2^128 = 2^256, which makes carries run across several limbs.
    bignum_from_cstr(&a, "340282366920938463463374607431768211456", 10); // 2^128
    bignum_mul(&r, &a, &a);
    check_str("mul 2^128 squared",
        &r,
        // 2^256
        "115792089237316195423570985008687907853269984665640564039457584007913129639936");

    bignum_destroy(&a); bignum_destroy(&b); bignum_destroy(&r);
}

/**
 * Checks q*b + r == a and |r| < |b|. That tests divmod and multiplication
 * against each other without precomputed answers. Returns 1 if both hold.
 */
static int check_divmod_reconstruction(const char *label, const bignum_t *a, const bignum_t *b,
                                        const bignum_t *q, const bignum_t *r) {
    bignum_t prod, sum;
    bignum_init(&prod, allocator_heap()); bignum_init(&sum, allocator_heap());
    bignum_mul(&prod, q, b);
    bignum_add(&sum, &prod, r);
    int ok = bignum_cmp(&sum, a) == 0;
    if (!ok) {
        char *s1 = bignum_to_str(&sum, 10), *s2 = bignum_to_str(a, 10);
        printf("FAIL %s reconstruction: got %s expected %s\n", label, s1, s2);
        failures++;
        free(s1); free(s2);
    }
    if (!bignum_is_zero(r) && bignum_cmp_mag(r, b) >= 0) {
        printf("FAIL %s: |remainder| >= |divisor|\n", label);
        failures++;
        ok = 0;
    }
    bignum_destroy(&prod); bignum_destroy(&sum);
    return ok;
}

/** Division with one-limb and multi-limb divisors, signs, exact and tiny dividends. */
static void test_divmod(void) {
    bignum_t a, b, q, r;
    bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap()); bignum_init(&q, allocator_heap()); bignum_init(&r, allocator_heap());

    bignum_from_cstr(&a, "100", 10);
    bignum_from_cstr(&b, "7", 10);
    bignum_divmod(&q, &r, &a, &b);
    check_str("100/7 q", &q, "14");
    check_str("100/7 r", &r, "2");

    // divisor of one limb
    bignum_from_cstr(&a, "123456789012345678901234567890", 10);
    bignum_from_cstr(&b, "999999937", 10); // prime, one limb
    bignum_divmod(&q, &r, &a, &b);
    check_divmod_reconstruction("single-limb divmod", &a, &b, &q, &r);

    // divisor of several limbs (Knuth's algorithm D)
    bignum_from_cstr(&a, "123456789012345678901234567890123456789012345678901234567890", 10);
    bignum_from_cstr(&b, "987654321098765432109876543210", 10); // above 2^32, so it takes the Knuth path
    bignum_divmod(&q, &r, &a, &b);
    check_divmod_reconstruction("multi-limb divmod", &a, &b, &q, &r);

    // Truncating division like C: -7 / 2 is -3 remainder -1.
    bignum_from_cstr(&a, "-7", 10);
    bignum_from_cstr(&b, "2", 10);
    bignum_divmod(&q, &r, &a, &b);
    check_str("-7/2 q (truncating)", &q, "-3");
    check_str("-7/2 r (truncating)", &r, "-1");

    // exact division
    bignum_from_cstr(&a, "144", 10);
    bignum_from_cstr(&b, "12", 10);
    bignum_divmod(&q, &r, &a, &b);
    check_str("exact division q", &q, "12");
    check_str("exact division r", &r, "0");

    // a equals b
    bignum_from_cstr(&a, "-55", 10);
    bignum_from_cstr(&b, "-55", 10);
    bignum_divmod(&q, &r, &a, &b);
    check_str("a == b q", &q, "1");
    check_str("a == b r", &r, "0");

    // |a| < |b|
    bignum_from_cstr(&a, "3", 10);
    bignum_from_cstr(&b, "10", 10);
    bignum_divmod(&q, &r, &a, &b);
    check_str("a smaller than b q", &q, "0");
    check_str("a smaller than b r", &r, "3");

    bignum_destroy(&a); bignum_destroy(&b); bignum_destroy(&q); bignum_destroy(&r);
}

/**
 * Large operands for the Karatsuba path in bignum_mul, which the int64-sized
 * random cross-check never reaches.
 */
static void test_karatsuba_large(void) {
    srand(2024);
    struct { int da, db; const char *label; } cases[] = {
        { 400,  400, "balanced, both above threshold"   },
        {3000,   50, "huge vs tiny"                     },
        {1500,  900, "both large, unequal"               },
        { 309,  309, "near the karatsuba/schoolbook boundary" },
        // Both operands are above the Karatsuba threshold (32 limbs), but
        // lopsided enough that the split leaves one high half empty (bn <= half).
        // That used to overflow dst in add_into, because the empty half was
        // still placed at offset 2*half without a bounds check.
        {3000,  350, "asymmetric, smaller operand still above threshold" },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *sa = random_decimal_string(cases[i].da);
        char *sb = random_decimal_string(cases[i].db);

        bignum_t a, b, prod, q, r;
        bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap()); bignum_init(&prod, allocator_heap()); bignum_init(&q, allocator_heap()); bignum_init(&r, allocator_heap());
        bignum_from_cstr(&a, sa, 10);
        bignum_from_cstr(&b, sb, 10);
        bignum_mul(&prod, &a, &b);

        bignum_divmod(&q, &r, &prod, &a);
        if (!bignum_is_zero(&r) || bignum_cmp(&q, &b) != 0) {
            printf("FAIL karatsuba (%s): prod/a != b\n", cases[i].label);
            failures++;
        }
        bignum_divmod(&q, &r, &prod, &b);
        if (!bignum_is_zero(&r) || bignum_cmp(&q, &a) != 0) {
            printf("FAIL karatsuba (%s): prod/b != a\n", cases[i].label);
            failures++;
        }

        bignum_destroy(&a); bignum_destroy(&b); bignum_destroy(&prod);
        bignum_destroy(&q); bignum_destroy(&r);
        free(sa); free(sb);
    }
}

/** Deep copy, negating zero, comparing zeros. */
static void test_edge_cases(void) {
    bignum_t a, b;
    bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap());

    // bignum_copy is a deep copy, changing the source must not touch the copy.
    bignum_from_cstr(&a, "123456789012345678901234567890", 10);
    bignum_copy(&b, &a);
    bignum_from_cstr(&a, "1", 10);
    check_str("copy independence", &b, "123456789012345678901234567890");

    // Negating zero keeps it positive.
    bignum_from_cstr(&a, "0", 10);
    bignum_negate(&a);
    if (a.sign != 1) { printf("FAIL negate zero sign: got %d, expected 1\n", a.sign); failures++; }

    // -0 and 0 compare equal even if the sign fields differed before normalizing.
    bignum_from_cstr(&a, "0", 10);
    bignum_from_cstr(&b, "-0", 10);
    if (bignum_cmp(&a, &b) != 0) { printf("FAIL 0 vs -0 comparison\n"); failures++; }

    bignum_destroy(&a); bignum_destroy(&b);
}

/** Building rationals from ints, bignums and strings. */
static void test_bigrational_construction(void) {
    bigrational_t q;
    bigrational_init(&q);
    check_rat_str("init default is 0", &q, 10, "0");

    bignum_t num, denom;
    bignum_init(&num, allocator_heap()); bignum_init(&denom, allocator_heap());
    bignum_from_cstr(&num, "4", 10);
    bignum_from_cstr(&denom, "8", 10);
    if (bigrational_from_bignum(&q, &num, &denom) != 0) {
        printf("FAIL from_bignum rejected valid input\n"); failures++;
    }
    check_rat_str("from_bignum reduces", &q, 10, "1/2");

    bignum_from_cstr(&denom, "0", 10);
    if (bigrational_from_bignum(&q, &num, &denom) != -1) {
        printf("FAIL from_bignum accepted zero denominator\n"); failures++;
    }
    bignum_destroy(&num); bignum_destroy(&denom);

    if (bigrational_from_int(&q, 6, -9) != 0) { printf("FAIL from_int rejected valid input\n"); failures++; }
    check_rat_str("from_int reduces and normalizes sign", &q, 10, "-2/3");

    if (bigrational_from_int(&q, 5, 0) != -1) { printf("FAIL from_int accepted zero denominator\n"); failures++; }

    if (bigrational_from_int(&q, 4, 1) != 0) { printf("FAIL from_int rejected valid input\n"); failures++; }
    check_rat_str("from_int integer form has no denominator", &q, 10, "4");

    if (bigrational_from_str(&q, "3/4", 10) != 0) { printf("FAIL from_str rejected valid fraction\n"); failures++; }
    check_rat_str("from_str fraction", &q, 10, "3/4");

    if (bigrational_from_str(&q, "10/4", 10) != 0) { printf("FAIL from_str rejected valid fraction\n"); failures++; }
    check_rat_str("from_str reduces", &q, 10, "5/2");

    if (bigrational_from_str(&q, "-7/2", 10) != 0) { printf("FAIL from_str rejected negative numerator\n"); failures++; }
    check_rat_str("from_str negative numerator", &q, 10, "-7/2");

    if (bigrational_from_str(&q, "7/-2", 10) != 0) { printf("FAIL from_str rejected negative denominator\n"); failures++; }
    check_rat_str("from_str negative denominator normalizes", &q, 10, "-7/2");

    if (bigrational_from_str(&q, "42", 10) != 0) { printf("FAIL from_str rejected integer form\n"); failures++; }
    check_rat_str("from_str integer only", &q, 10, "42");

    if (bigrational_from_str(&q, "ff/10", 16) != 0) { printf("FAIL from_str rejected hex fraction\n"); failures++; }
    check_rat_str("from_str hex fraction", &q, 10, "255/16");

    if (bigrational_from_str(&q, "1/0", 10) != -1) { printf("FAIL from_str accepted zero denominator\n"); failures++; }
    if (bigrational_from_str(&q, "abc/2", 10) != -1) { printf("FAIL from_str accepted invalid numerator\n"); failures++; }
    if (bigrational_from_str(&q, "2/abc", 10) != -1) { printf("FAIL from_str accepted invalid denominator\n"); failures++; }
    if (bigrational_from_str(&q, "", 10) != -1) { printf("FAIL from_str accepted empty string\n"); failures++; }

    // from_str must not write to the caller's string. String literals are
    // read-only, so a write would crash.
    const char *lit = "9/8";
    if (bigrational_from_str(&q, lit, 10) != 0) { printf("FAIL from_str rejected literal input\n"); failures++; }
    check_rat_str("from_str leaves input string usable", &q, 10, "9/8");
    if (strcmp(lit, "9/8") != 0) { printf("FAIL from_str mutated its input string\n"); failures++; }

    bigrational_destroy(&q);
}

/** Add, sub, mul, div on fractions, including reduction and division by zero. */
static void test_bigrational_arithmetic(void) {
    bigrational_t a, b, r;
    bigrational_init(&a); bigrational_init(&b); bigrational_init(&r);

    bigrational_from_str(&a, "1/2", 10);
    bigrational_from_str(&b, "1/3", 10);
    bigrational_add(&r, &a, &b);
    check_rat_str("1/2 + 1/3", &r, 10, "5/6");

    bigrational_sub(&r, &a, &b);
    check_rat_str("1/2 - 1/3", &r, 10, "1/6");

    bigrational_mul(&r, &a, &b);
    check_rat_str("1/2 * 1/3", &r, 10, "1/6");

    if (bigrational_div(&r, &a, &b) != 0) { printf("FAIL div rejected valid divisor\n"); failures++; }
    check_rat_str("1/2 / 1/3", &r, 10, "3/2");

    // The result is reduced even when the cross-multiplied intermediate isn't.
    bigrational_from_str(&a, "2/3", 10);
    bigrational_from_str(&b, "1/3", 10);
    bigrational_add(&r, &a, &b);
    check_rat_str("2/3 + 1/3 reduces to integer", &r, 10, "1");

    // adding a negative fraction
    bigrational_from_str(&a, "1/2", 10);
    bigrational_from_str(&b, "-1/2", 10);
    bigrational_add(&r, &a, &b);
    check_rat_str("1/2 + -1/2", &r, 10, "0");

    // Division by zero fails and leaves dst as it was.
    bigrational_from_str(&a, "1/2", 10);
    bigrational_from_str(&b, "0", 10);
    if (bigrational_div(&r, &a, &b) != -1) { printf("FAIL div by zero accepted\n"); failures++; }

    // multiplying by zero
    bigrational_from_str(&a, "5/7", 10);
    bigrational_from_str(&b, "0", 10);
    bigrational_mul(&r, &a, &b);
    check_rat_str("mul by zero", &r, 10, "0");

    // Integers behave like plain bignum arithmetic.
    bigrational_from_int(&a, 7, 1);
    bigrational_from_int(&b, 3, 1);
    bigrational_mul(&r, &a, &b);
    check_rat_str("7 * 3 as rationals", &r, 10, "21");

    bigrational_destroy(&a); bigrational_destroy(&b); bigrational_destroy(&r);
}

/** Ordering and is_zero. */
static void test_bigrational_cmp_and_predicates(void) {
    bigrational_t a, b;
    bigrational_init(&a); bigrational_init(&b);

    bigrational_from_str(&a, "1/2", 10);
    bigrational_from_str(&b, "2/4", 10);
    if (bigrational_cmp(&a, &b) != 0) { printf("FAIL 1/2 vs 2/4 should compare equal\n"); failures++; }

    bigrational_from_str(&a, "1/3", 10);
    bigrational_from_str(&b, "1/2", 10);
    if (bigrational_cmp(&a, &b) >= 0) { printf("FAIL 1/3 should compare less than 1/2\n"); failures++; }
    if (bigrational_cmp(&b, &a) <= 0) { printf("FAIL 1/2 should compare greater than 1/3\n"); failures++; }

    bigrational_from_str(&a, "-1/2", 10);
    bigrational_from_str(&b, "1/2", 10);
    if (bigrational_cmp(&a, &b) >= 0) { printf("FAIL -1/2 should compare less than 1/2\n"); failures++; }

    bigrational_from_str(&a, "0", 10);
    if (!bigrational_is_zero(&a)) { printf("FAIL 0 should be zero\n"); failures++; }
    bigrational_from_str(&a, "0/5", 10);
    if (!bigrational_is_zero(&a)) { printf("FAIL 0/5 should be zero\n"); failures++; }
    bigrational_from_str(&a, "1/5", 10);
    if (bigrational_is_zero(&a)) { printf("FAIL 1/5 should not be zero\n"); failures++; }

    bigrational_destroy(&a); bigrational_destroy(&b);
}

/** Negation and inversion, including signs and zero. */
static void test_bigrational_negate_invert(void) {
    bigrational_t a;
    bigrational_init(&a);

    bigrational_from_str(&a, "-3/4", 10);
    bigrational_negate(&a);
    check_rat_str("negate -3/4", &a, 10, "3/4");
    bigrational_negate(&a);
    check_rat_str("negate back to -3/4", &a, 10, "-3/4");

    bigrational_from_str(&a, "0", 10);
    bigrational_negate(&a);
    check_rat_str("negate zero stays zero", &a, 10, "0");

    bigrational_from_str(&a, "3/4", 10);
    if (bigrational_invert(&a) != 0) { printf("FAIL invert rejected nonzero value\n"); failures++; }
    check_rat_str("invert 3/4", &a, 10, "4/3");

    bigrational_from_str(&a, "-3/4", 10);
    if (bigrational_invert(&a) != 0) { printf("FAIL invert rejected nonzero value\n"); failures++; }
    check_rat_str("invert -3/4 keeps sign on numerator", &a, 10, "-4/3");

    bigrational_from_str(&a, "5", 10);
    if (bigrational_invert(&a) != 0) { printf("FAIL invert rejected integer\n"); failures++; }
    check_rat_str("invert integer", &a, 10, "1/5");

    bigrational_from_str(&a, "0", 10);
    if (bigrational_invert(&a) != -1) { printf("FAIL invert accepted zero\n"); failures++; }

    bigrational_destroy(&a);
}

/**
 * bignum_gcd is static in bigrational.c, so this tests it through reduce():
 * every case expects an exactly reduced fraction, which only comes out right if
 * the gcd is.
 */
static void test_bigrational_gcd_reduction(void) {
    bigrational_t q;
    bigrational_init(&q);

    bigrational_from_str(&q, "7/7", 10);
    check_rat_str("gcd equal inputs", &q, 10, "1");

    bigrational_from_str(&q, "6/18", 10);
    check_rat_str("gcd one divides other", &q, 10, "1/3");

    bigrational_from_str(&q, "17/13", 10);
    check_rat_str("gcd coprime pair", &q, 10, "17/13");

    bigrational_from_str(&q, "0/9", 10);
    check_rat_str("gcd zero numerator", &q, 10, "0");

    bigrational_from_str(&q, "48/36", 10);
    check_rat_str("gcd both even", &q, 10, "4/3");

    bigrational_from_str(&q, "1024/768", 10);
    check_rat_str("gcd shared power of two", &q, 10, "4/3");

    // Large multi-limb operands, so the binary gcd runs across many limbs:
    // num = 3 * 2^300 and denom = 5 * 2^300 reduce to 3/5.
    bignum_t two, e300, three, five, num, denom;
    bignum_init(&two, allocator_heap()); bignum_init(&e300, allocator_heap()); bignum_init(&three, allocator_heap());
    bignum_init(&five, allocator_heap()); bignum_init(&num, allocator_heap()); bignum_init(&denom, allocator_heap());
    bignum_from_i64(&two, 2);
    bignum_pow_u64(&e300, &two, 300);
    bignum_from_i64(&three, 3);
    bignum_from_i64(&five, 5);
    bignum_mul(&num, &three, &e300);
    bignum_mul(&denom, &five, &e300);
    if (bigrational_from_bignum(&q, &num, &denom) != 0) { printf("FAIL large gcd construction\n"); failures++; }
    check_rat_str("gcd large multi-limb shared factor", &q, 10, "3/5");
    bignum_destroy(&two); bignum_destroy(&e300); bignum_destroy(&three);
    bignum_destroy(&five); bignum_destroy(&num); bignum_destroy(&denom);

    bigrational_destroy(&q);
}

/** Runs the rational tests. */
static void run_bigrational_tests(void) {
    test_bigrational_construction();
    test_bigrational_arithmetic();
    test_bigrational_cmp_and_predicates();
    test_bigrational_negate_invert();
    test_bigrational_gcd_reduction();
}

// bignum_pow_u64

/** Powers of small and large bases, exponent 0 and 1, and dst aliasing base. */
static void test_bignum_pow_u64(void) {
    bignum_t base, r;
    bignum_init(&base, allocator_heap()); bignum_init(&r, allocator_heap());

    bignum_from_i64(&base, 2);
    bignum_pow_u64(&r, &base, 0);
    check_str("2^0", &r, "1");

    bignum_pow_u64(&r, &base, 1);
    check_str("2^1", &r, "2");

    bignum_pow_u64(&r, &base, 10);
    check_str("2^10", &r, "1024");

    bignum_from_i64(&base, 3);
    bignum_pow_u64(&r, &base, 100);
    check_str("3^100", &r,
        "515377520732011331036461129765621272702107522001");

    // dst is the same as base
    bignum_from_i64(&base, 5);
    bignum_pow_u64(&base, &base, 4);
    check_str("5^4 dst aliases base", &base, "625");

    bignum_destroy(&base); bignum_destroy(&r);
}

// bigfloat

/** Building floats from ints, bignums, ratios and strings. */
static void test_bigfloat_construction(void) {
    // For an exact round trip the precision has to cover the bit length of
    // the value, or round_mantissa rounds it. 128 bits is plenty here.
    bigfloat_t f;
    bigfloat_init(&f, 128);

    bigfloat_from_i64(&f, 42);
    check_bigfloat_str("from_i64 positive", &f, 0, "42");

    bigfloat_from_i64(&f, -42);
    check_bigfloat_str("from_i64 negative", &f, 0, "-42");

    bigfloat_from_i64(&f, 0);
    check_bigfloat_str("from_i64 zero", &f, 0, "0");
    if (!bigfloat_is_zero(&f)) { printf("FAIL from_i64 zero should be zero kind\n"); failures++; }

    // about 97 bits, exact at 128
    bignum_t n;
    bignum_init(&n, allocator_heap());
    bignum_from_cstr(&n, "123456789012345678901234567890", 10);
    bigfloat_from_bignum(&f, &n);
    check_bigfloat_str("from_bignum big", &f, 0, "123456789012345678901234567890");
    bignum_destroy(&n);

    bignum_t num, denom;
    bignum_init(&num, allocator_heap()); bignum_init(&denom, allocator_heap());
    bignum_from_i64(&num, 1);
    bignum_from_i64(&denom, 4);
    bigfloat_from_ratio(&f, &num, &denom);
    check_bigfloat_str("from_ratio 1/4", &f, 10, "0.25");
    bignum_destroy(&num); bignum_destroy(&denom);

    // 3.125 = 3 + 1/8 is exact in binary. A decimal like 3.14159 is not at
    // any precision, so it can't be used for an exact round trip.
    if (bigfloat_from_str(&f, "3.125", 128) != 0) { printf("FAIL from_str rejected valid decimal\n"); failures++; }
    check_bigfloat_str("from_str decimal", &f, 3, "3.125");

    if (bigfloat_from_str(&f, "-2.5", 128) != 0) { printf("FAIL from_str rejected negative\n"); failures++; }
    check_bigfloat_str("from_str negative", &f, 1, "-2.5");

    if (bigfloat_from_str(&f, "1.5e3", 128) != 0) { printf("FAIL from_str rejected exponent notation\n"); failures++; }
    check_bigfloat_str("from_str positive exponent", &f, 0, "1500");

    // 125e-3 = 0.125 = 1/8, also exact
    if (bigfloat_from_str(&f, "125e-3", 128) != 0) { printf("FAIL from_str rejected negative exponent\n"); failures++; }
    check_bigfloat_str("from_str negative exponent", &f, 4, "0.125");

    if (bigfloat_from_str(&f, "0", 128) != 0) { printf("FAIL from_str rejected zero\n"); failures++; }
    if (!bigfloat_is_zero(&f)) { printf("FAIL from_str \"0\" should be zero\n"); failures++; }

    if (bigfloat_from_str(&f, "abc", 128) != -1) { printf("FAIL from_str accepted invalid input\n"); failures++; }
    if (bigfloat_from_str(&f, "", 128) != -1) { printf("FAIL from_str accepted empty string\n"); failures++; }

    bigfloat_free(&f);
}

/** Exactly representable values print back unchanged. */
static void test_bigfloat_to_str_roundtrip(void) {
    bigfloat_t f;
    bigfloat_init(&f, 256);

    // 50-digit integer, needs about 166 of the 256 bits
    bigfloat_from_str(&f, "12345678901234567890123456789012345678901234567890", 256);
    check_bigfloat_str("roundtrip high precision integer", &f, 9, "12345678901234567890123456789012345678901234567890");

    // An integer prints without a fraction even if more digits are asked
    // for: the loop stops once the remainder is zero.
    bigfloat_from_str(&f, "1", 256);
    check_bigfloat_str("integer roundtrip has no trailing zero fraction", &f, 5, "1");

    // -0.5 is exact
    bigfloat_from_str(&f, "-0.5", 256);
    check_bigfloat_str("roundtrip negative fraction", &f, 1, "-0.5");

    // 1/16 is exact too and needs more fraction digits
    bigfloat_from_str(&f, "0.0625", 256);
    check_bigfloat_str("roundtrip small dyadic fraction", &f, 4, "0.0625");

    bigfloat_free(&f);
}

/** Zero, infinity and NaN through every operation, following IEEE. */
static void test_bigfloat_special_values(void) {
    bigfloat_t z, negz, inf, neginf, nan, one, r;
    bigfloat_init(&z, 64); bigfloat_init(&negz, 64); bigfloat_init(&inf, 64);
    bigfloat_init(&neginf, 64); bigfloat_init(&nan, 64); bigfloat_init(&one, 64);
    bigfloat_init(&r, 64);

    bigfloat_set_zero(&z, 1);
    bigfloat_set_zero(&negz, -1);
    bigfloat_set_inf(&inf, 1);
    bigfloat_set_inf(&neginf, -1);
    bigfloat_set_nan(&nan);
    bigfloat_from_i64(&one, 1);

    bigfloat_add(&r, &nan, &one);
    if (!bigfloat_is_nan(&r)) { printf("FAIL nan+1 should be nan\n"); failures++; }
    bigfloat_add(&r, &inf, &inf);
    if (!bigfloat_is_inf(&r) || r.sign != 1) { printf("FAIL inf+inf should be +inf\n"); failures++; }
    bigfloat_add(&r, &inf, &neginf);
    if (!bigfloat_is_nan(&r)) { printf("FAIL inf+-inf should be nan\n"); failures++; }
    bigfloat_add(&r, &inf, &one);
    if (!bigfloat_is_inf(&r) || r.sign != 1) { printf("FAIL inf+1 should be inf\n"); failures++; }
    bigfloat_add(&r, &z, &z);
    if (!bigfloat_is_zero(&r) || r.sign != 1) { printf("FAIL 0+0 should be +0\n"); failures++; }
    bigfloat_add(&r, &negz, &negz);
    if (!bigfloat_is_zero(&r) || r.sign != -1) { printf("FAIL -0+-0 should be -0\n"); failures++; }
    bigfloat_add(&r, &z, &one);
    check_bigfloat_str("0+1", &r, 0, "1");

    bigfloat_mul(&r, &inf, &z);
    if (!bigfloat_is_nan(&r)) { printf("FAIL inf*0 should be nan\n"); failures++; }
    bigfloat_mul(&r, &inf, &one);
    if (!bigfloat_is_inf(&r)) { printf("FAIL inf*1 should be inf\n"); failures++; }
    bigfloat_mul(&r, &z, &one);
    if (!bigfloat_is_zero(&r)) { printf("FAIL 0*1 should be 0\n"); failures++; }
    bigfloat_mul(&r, &neginf, &one);
    if (!bigfloat_is_inf(&r) || r.sign != -1) { printf("FAIL -inf*1 should be -inf\n"); failures++; }
    bigfloat_mul(&r, &nan, &one);
    if (!bigfloat_is_nan(&r)) { printf("FAIL nan*1 should be nan\n"); failures++; }

    bigfloat_div(&r, &one, &z);
    if (!bigfloat_is_inf(&r)) { printf("FAIL 1/0 should be inf\n"); failures++; }
    bigfloat_div(&r, &z, &z);
    if (!bigfloat_is_nan(&r)) { printf("FAIL 0/0 should be nan\n"); failures++; }
    bigfloat_div(&r, &inf, &inf);
    if (!bigfloat_is_nan(&r)) { printf("FAIL inf/inf should be nan\n"); failures++; }
    bigfloat_div(&r, &inf, &one);
    if (!bigfloat_is_inf(&r)) { printf("FAIL inf/1 should be inf\n"); failures++; }
    bigfloat_div(&r, &one, &inf);
    if (!bigfloat_is_zero(&r)) { printf("FAIL 1/inf should be 0\n"); failures++; }
    bigfloat_div(&r, &z, &one);
    if (!bigfloat_is_zero(&r)) { printf("FAIL 0/1 should be 0\n"); failures++; }

    bigfloat_free(&z); bigfloat_free(&negz); bigfloat_free(&inf);
    bigfloat_free(&neginf); bigfloat_free(&nan); bigfloat_free(&one); bigfloat_free(&r);
}

/** Add, sub, mul, div on finite values, plus a cross-check against bignum. */
static void test_bigfloat_arithmetic(void) {
    bigfloat_t a, b, r;
    bigfloat_init(&a, 128); bigfloat_init(&b, 128); bigfloat_init(&r, 128);

    bigfloat_from_str(&a, "1.5", 128);
    bigfloat_from_str(&b, "2.25", 128);
    bigfloat_add(&r, &a, &b);
    check_bigfloat_str("1.5 + 2.25", &r, 2, "3.75");

    bigfloat_sub(&r, &a, &b);
    check_bigfloat_str("1.5 - 2.25", &r, 2, "-0.75");

    bigfloat_mul(&r, &a, &b);
    check_bigfloat_str("1.5 * 2.25", &r, 3, "3.375");

    bigfloat_div(&r, &b, &a);
    check_bigfloat_str("2.25 / 1.5", &r, 1, "1.5");

    // High precision, so bigfloat_mul takes the Karatsuba path.
    bigfloat_t big_a, big_b, big_r;
    bigfloat_init(&big_a, 4096); bigfloat_init(&big_b, 4096); bigfloat_init(&big_r, 4096);
    char *sa = random_decimal_string(400);
    char *sb = random_decimal_string(400);
    bigfloat_from_str(&big_a, sa, 4096);
    bigfloat_from_str(&big_b, sb, 4096);
    bigfloat_mul(&big_r, &big_a, &big_b);
    // Compare with bignum multiplication of the same integers.
    bignum_t na, nb, nprod;
    bignum_init(&na, allocator_heap()); bignum_init(&nb, allocator_heap()); bignum_init(&nprod, allocator_heap());
    bignum_from_cstr(&na, sa, 10);
    bignum_from_cstr(&nb, sb, 10);
    bignum_mul(&nprod, &na, &nb);
    char *expect = bignum_to_str(&nprod, 10);
    check_bigfloat_str("large mul matches bignum", &big_r, 0, expect);
    free(expect);
    bignum_destroy(&na); bignum_destroy(&nb); bignum_destroy(&nprod);
    bigfloat_free(&big_a); bigfloat_free(&big_b); bigfloat_free(&big_r);
    free(sa); free(sb);

    bigfloat_free(&a); bigfloat_free(&b); bigfloat_free(&r);
}

/** Ordering, including values of different precision. */
static void test_bigfloat_cmp(void) {
    bigfloat_t a, b;
    bigfloat_init(&a, 64); bigfloat_init(&b, 64);

    bigfloat_from_i64(&a, 5);
    bigfloat_from_i64(&b, 5);
    if (bigfloat_cmp(&a, &b) != 0) { printf("FAIL 5 vs 5 should be equal\n"); failures++; }

    bigfloat_from_i64(&a, 3);
    bigfloat_from_i64(&b, 7);
    if (bigfloat_cmp(&a, &b) >= 0) { printf("FAIL 3 vs 7 should be less\n"); failures++; }
    if (bigfloat_cmp(&b, &a) <= 0) { printf("FAIL 7 vs 3 should be greater\n"); failures++; }

    bigfloat_from_i64(&a, -3);
    bigfloat_from_i64(&b, 3);
    if (bigfloat_cmp(&a, &b) >= 0) { printf("FAIL -3 vs 3 should be less\n"); failures++; }

    // Regression: the same value at two precisions can have different
    // mantissa lengths at the same exponent, since rounding trims but never
    // pads. The comparison has to go by value, not by limb count like
    // cmp_mag_raw does, which is why bigfloat_cmp pads with zeros itself.
    bigfloat_t hi, lo;
    bigfloat_init(&hi, 256);
    bigfloat_init(&lo, 32);
    bignum_t num, denom;
    bignum_init(&num, allocator_heap()); bignum_init(&denom, allocator_heap());
    bignum_from_i64(&num, 1); bignum_from_i64(&denom, 1);
    bigfloat_from_ratio(&hi, &num, &denom);
    bigfloat_from_ratio(&lo, &num, &denom);
    if (hi.mlen == lo.mlen) { printf("FAIL test setup: expected different mantissa lengths (hi=%zu lo=%zu)\n", hi.mlen, lo.mlen); failures++; }
    if (bigfloat_cmp(&hi, &lo) != 0) { printf("FAIL equal value at different precisions should compare equal\n"); failures++; }
    bignum_destroy(&num); bignum_destroy(&denom);
    bigfloat_free(&hi); bigfloat_free(&lo);

    bigfloat_free(&a); bigfloat_free(&b);
}

/**
 * Runs the same operation as float and as rational, converts the rational
 * result with from_ratio at the same precision, and compares both as decimal
 * strings. bigrational_to_str prints "n/d", so it can't be compared to
 * bigfloat_to_str directly.
 */
static void test_bigfloat_bigrational_cross_check(void) {
    srand(99);
    const size_t precision = 128;
    const int decimal_digits = 15;

    for (int iter = 0; iter < 500; iter++) {
        int64_t x = (int64_t)(rand() % 100000) - 50000;
        int64_t y = (int64_t)(rand() % 100000) - 50000;
        if (x == 0) x = 1;
        if (y == 0) y = 1;

        bigrational_t ra, rb, rsum, rdiff, rprod, rquot;
        bigrational_init(&ra); bigrational_init(&rb);
        bigrational_init(&rsum); bigrational_init(&rdiff);
        bigrational_init(&rprod); bigrational_init(&rquot);
        bigrational_from_int(&ra, x, 1);
        bigrational_from_int(&rb, y, 1);
        bigrational_add(&rsum, &ra, &rb);
        bigrational_sub(&rdiff, &ra, &rb);
        bigrational_mul(&rprod, &ra, &rb);
        bigrational_div(&rquot, &ra, &rb);

        bigfloat_t fa, fb, fsum, fdiff, fprod, fquot;
        bigfloat_init(&fa, precision); bigfloat_init(&fb, precision);
        bigfloat_init(&fsum, precision); bigfloat_init(&fdiff, precision);
        bigfloat_init(&fprod, precision); bigfloat_init(&fquot, precision);
        bigfloat_from_i64(&fa, x);
        bigfloat_from_i64(&fb, y);
        bigfloat_add(&fsum, &fa, &fb);
        bigfloat_sub(&fdiff, &fa, &fb);
        bigfloat_mul(&fprod, &fa, &fb);
        bigfloat_div(&fquot, &fa, &fb);

        struct { const char *label; bigrational_t *r; bigfloat_t *f; } cases[] = {
            {"add", &rsum, &fsum}, {"sub", &rdiff, &fdiff},
            {"mul", &rprod, &fprod}, {"div", &rquot, &fquot},
        };
        for (int c = 0; c < 4; c++) {
            bigfloat_t from_rat;
            bigfloat_init(&from_rat, precision);
            bigfloat_from_ratio(&from_rat, &cases[c].r->num, &cases[c].r->denom);
            char *s1 = bigfloat_to_str(&from_rat, decimal_digits);
            char *s2 = bigfloat_to_str(cases[c].f, decimal_digits);
            if (strcmp(s1, s2) != 0) {
                printf("FAIL cross-check %s: rational-derived=%s float=%s (x=%lld y=%lld)\n",
                       cases[c].label, s1, s2, (long long)x, (long long)y);
                failures++;
            }
            free(s1); free(s2);
            bigfloat_free(&from_rat);
        }

        bigrational_destroy(&ra); bigrational_destroy(&rb);
        bigrational_destroy(&rsum); bigrational_destroy(&rdiff);
        bigrational_destroy(&rprod); bigrational_destroy(&rquot);
        bigfloat_free(&fa); bigfloat_free(&fb);
        bigfloat_free(&fsum); bigfloat_free(&fdiff);
        bigfloat_free(&fprod); bigfloat_free(&fquot);
    }
}

/** Runs the float tests. */
static void run_bigfloat_tests(void) {
    test_bigfloat_construction();
    test_bigfloat_to_str_roundtrip();
    test_bigfloat_special_values();
    test_bigfloat_arithmetic();
    test_bigfloat_cmp();
    test_bigfloat_bigrational_cross_check();
}

/** Random operands checked against __int128. Catches sign and carry edge cases. */
static void test_random_cross_check(void) {
    srand(42);
    for (int iter = 0; iter < 200000; iter++) {
        int64_t x = ((int64_t)rand() << 32) ^ rand();
        int64_t y = ((int64_t)rand() << 32) ^ rand();
        if (y == 0) y = 1;

        bignum_t a, b, r;
        bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap()); bignum_init(&r, allocator_heap());
        bignum_from_i64(&a, x);
        bignum_from_i64(&b, y);

        bignum_add(&r, &a, &b);
        __int128 expect_add = (__int128)x + y;
        char *s = bignum_to_str(&r, 10);
        char expect_buf[64];
        // x+y needs 65 bits at most, so print as int64 when it fits and
        // skip splitting the __int128 into halves.
        if (expect_add >= INT64_MIN && expect_add <= INT64_MAX) {
            snprintf(expect_buf, sizeof(expect_buf), "%lld", (long long)expect_add);
            if (strcmp(s, expect_buf) != 0) {
                printf("FAIL random add: %lld + %lld = %s, expected %s\n",
                       (long long)x, (long long)y, s, expect_buf);
                failures++;
            }
        }
        free(s);

        bignum_t sub;
        bignum_init(&sub, allocator_heap());
        bignum_sub(&sub, &a, &b);
        __int128 expect_sub = (__int128)x - y;
        s = bignum_to_str(&sub, 10);
        if (expect_sub >= INT64_MIN && expect_sub <= INT64_MAX) {
            snprintf(expect_buf, sizeof(expect_buf), "%lld", (long long)expect_sub);
            if (strcmp(s, expect_buf) != 0) {
                printf("FAIL random sub: %lld - %lld = %s, expected %s\n",
                       (long long)x, (long long)y, s, expect_buf);
                failures++;
            }
        }
        free(s);
        bignum_destroy(&sub);

        bignum_t m;
        bignum_init(&m, allocator_heap());
        bignum_mul(&m, &a, &b);
        __int128 expect_mul = (__int128)x * y;
        s = bignum_to_str(&m, 10);
        if (expect_mul >= INT64_MIN && expect_mul <= INT64_MAX) {
            snprintf(expect_buf, sizeof(expect_buf), "%lld", (long long)expect_mul);
            if (strcmp(s, expect_buf) != 0) {
                printf("FAIL random mul: %lld * %lld = %s, expected %s\n",
                       (long long)x, (long long)y, s, expect_buf);
                failures++;
            }
        }
        free(s);

        bignum_t q, rem;
        bignum_init(&q, allocator_heap()); bignum_init(&rem, allocator_heap());
        bignum_divmod(&q, &rem, &a, &b);
        int64_t expect_q = x / y;
        int64_t expect_r = x % y;
        s = bignum_to_str(&q, 10);
        snprintf(expect_buf, sizeof(expect_buf), "%lld", (long long)expect_q);
        if (strcmp(s, expect_buf) != 0) {
            printf("FAIL random div q: %lld / %lld = %s, expected %s\n",
                   (long long)x, (long long)y, s, expect_buf);
            failures++;
        }
        free(s);
        s = bignum_to_str(&rem, 10);
        snprintf(expect_buf, sizeof(expect_buf), "%lld", (long long)expect_r);
        if (strcmp(s, expect_buf) != 0) {
            printf("FAIL random div r: %lld %% %lld = %s, expected %s\n",
                   (long long)x, (long long)y, s, expect_buf);
            failures++;
        }
        free(s);

        bignum_destroy(&a); bignum_destroy(&b); bignum_destroy(&r);
        bignum_destroy(&m); bignum_destroy(&q); bignum_destroy(&rem);
    }
}

/** Bignums backed by an arena: in-place and copying growth, release. */
static void test_arena_allocator(void) {
    arena_t arena;
    if (arena_init(&arena, 1 << 22) != 0) { printf("FAIL arena_init\n"); failures++; return; }
    allocator_t al = arena_allocator(&arena);

    // Operands and result live in the arena. The limbs grow over many
    // operations, sometimes in place (latest allocation), sometimes by copying.
    bignum_t a, b, c;
    bignum_init(&a, al); bignum_init(&b, al); bignum_init(&c, al);
    bignum_from_cstr(&a, "123456789012345678901234567890", 10);
    bignum_from_cstr(&b, "987654321098765432109876543210", 10);
    bignum_mul(&c, &a, &b);
    check_str("arena mul", &c, "121932631137021795226185032733622923332237463801111263526900");
    bignum_mul(&a, &a, &c);
    bignum_mul(&b, &b, &a);
    bignum_divmod(&c, NULL, &b, &a);
    check_str("arena divmod", &c, "987654321098765432109876543210");

    char *big = random_decimal_string(400);
    bignum_from_cstr(&a, big, 10);
    bignum_mul(&b, &a, &a);
    bignum_divmod(&c, NULL, &b, &a);
    check_str("arena big roundtrip", &c, big);
    free(big);

    bignum_destroy(&c); bignum_destroy(&b); bignum_destroy(&a);

    // Destroying an arena-backed value must not touch the heap, and
    // releasing the arena frees everything at once.
    arena_release(&arena, (arena_mark_t){ 0 });
    if (arena.offset != 0) { printf("FAIL arena not released\n"); failures++; }
    else printf("PASS arena released\n");

    arena_destroy(&arena);
}

/** Runs the whole correctness suite. */
static void run_tests(void) {
    printf("-- Bignum tests\n");
    test_basic_roundtrip();
    test_string_parsing();
    test_bases();
    test_add_sub();
    test_mul();
    test_divmod();
    test_karatsuba_large();
    test_edge_cases();
    test_bignum_pow_u64();
    test_arena_allocator();
    printf("-- Bigrational tests\n");
    run_bigrational_tests();
    test_random_cross_check();
    printf("-- Bigfloat tests\n");
    run_bigfloat_tests();
}

// benchmarks

/** Monotonic time in seconds. */
static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

typedef void (*binop_fn)(bignum_t *, const bignum_t *, const bignum_t *);

/** Times a bignum binary operation on random operands. */
static void bench_binop(const char *label, binop_fn fn, int digits, int iters) {
    char *sa = random_decimal_string(digits);
    char *sb = random_decimal_string(digits);
    bignum_t a, b, r;
    bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap()); bignum_init(&r, allocator_heap());
    bignum_from_cstr(&a, sa, 10);
    bignum_from_cstr(&b, sb, 10);

    double start = now_seconds();
    for (int i = 0; i < iters; i++) fn(&r, &a, &b);
    double elapsed = now_seconds() - start;

    printf("  %-10s %6d digits  %8d iters  %9.3f ms  %10.3f us/op\n",
           label, digits, iters, elapsed * 1e3, elapsed * 1e6 / iters);

    bignum_destroy(&a); bignum_destroy(&b); bignum_destroy(&r);
    free(sa); free(sb);
}

/** Times bignum division for the given operand sizes. */
static void bench_divmod(int digits_a, int digits_b, int iters) {
    char *sa = random_decimal_string(digits_a);
    char *sb = random_decimal_string(digits_b);
    bignum_t a, b, q, r;
    bignum_init(&a, allocator_heap()); bignum_init(&b, allocator_heap()); bignum_init(&q, allocator_heap()); bignum_init(&r, allocator_heap());
    bignum_from_cstr(&a, sa, 10);
    bignum_from_cstr(&b, sb, 10);

    double start = now_seconds();
    for (int i = 0; i < iters; i++) bignum_divmod(&q, &r, &a, &b);
    double elapsed = now_seconds() - start;

    printf("  %-10s %3d/%-3d digits  %8d iters  %9.3f ms  %10.3f us/op\n",
           "divmod", digits_a, digits_b, iters, elapsed * 1e3, elapsed * 1e6 / iters);

    bignum_destroy(&a); bignum_destroy(&b); bignum_destroy(&q); bignum_destroy(&r);
    free(sa); free(sb);
}

/** Random "n/d" string with digits digits on each side. Not necessarily reduced. */
static char *random_rational_string(int digits) {
    char *num = random_decimal_string(digits);
    char *denom = random_decimal_string(digits);
    size_t len = strlen(num) + 1 + strlen(denom) + 1;
    char *s = malloc(len);
    snprintf(s, len, "%s/%s", num, denom);
    free(num);
    free(denom);
    return s;
}

typedef void (*rat_binop_fn)(bigrational_t *, const bigrational_t *, const bigrational_t *);

/** Times a rational binary operation. */
static void bench_rat_binop(const char *label, rat_binop_fn fn, int digits, int iters) {
    char *sa = random_rational_string(digits);
    char *sb = random_rational_string(digits);
    bigrational_t a, b, r;
    bigrational_init(&a); bigrational_init(&b); bigrational_init(&r);
    bigrational_from_str(&a, sa, 10);
    bigrational_from_str(&b, sb, 10);

    double start = now_seconds();
    for (int i = 0; i < iters; i++) fn(&r, &a, &b);
    double elapsed = now_seconds() - start;

    printf("  %-10s %6d digits  %8d iters  %9.3f ms  %10.3f us/op\n",
           label, digits, iters, elapsed * 1e3, elapsed * 1e6 / iters);

    bigrational_destroy(&a); bigrational_destroy(&b); bigrational_destroy(&r);
    free(sa); free(sb);
}

/** Times rational division. */
static void bench_rat_div(int digits, int iters) {
    char *sa = random_rational_string(digits);
    char *sb = random_rational_string(digits);
    bigrational_t a, b, r;
    bigrational_init(&a); bigrational_init(&b); bigrational_init(&r);
    bigrational_from_str(&a, sa, 10);
    bigrational_from_str(&b, sb, 10);

    double start = now_seconds();
    for (int i = 0; i < iters; i++) bigrational_div(&r, &a, &b);
    double elapsed = now_seconds() - start;

    printf("  %-10s %6d digits  %8d iters  %9.3f ms  %10.3f us/op\n",
           "rat div", digits, iters, elapsed * 1e3, elapsed * 1e6 / iters);

    bigrational_destroy(&a); bigrational_destroy(&b); bigrational_destroy(&r);
    free(sa); free(sb);
}

typedef void (*bigfloat_binop_fn)(bigfloat_t *, const bigfloat_t *, const bigfloat_t *);

/** Times a bigfloat binary operation. */
static void bench_bigfloat_binop(const char *label, bigfloat_binop_fn fn, size_t precision, int digits, int iters) {
    char *sa = random_decimal_string(digits);
    char *sb = random_decimal_string(digits);
    bigfloat_t a, b, r;
    bigfloat_init(&a, precision); bigfloat_init(&b, precision); bigfloat_init(&r, precision);
    bigfloat_from_str(&a, sa, precision);
    bigfloat_from_str(&b, sb, precision);

    double start = now_seconds();
    for (int i = 0; i < iters; i++) fn(&r, &a, &b);
    double elapsed = now_seconds() - start;

    printf("  %-10s %6zu bits  %6d digits  %8d iters  %9.3f ms  %10.3f us/op\n",
           label, precision, digits, iters, elapsed * 1e3, elapsed * 1e6 / iters);

    bigfloat_free(&a); bigfloat_free(&b); bigfloat_free(&r);
    free(sa); free(sb);
}

/** Times bigfloat printing. */
static void bench_bigfloat_to_str(size_t precision, int digits, int iters) {
    char *sa = random_decimal_string(digits);
    bigfloat_t a;
    bigfloat_init(&a, precision);
    bigfloat_from_str(&a, sa, precision);

    double start = now_seconds();
    for (int i = 0; i < iters; i++) { char *s = bigfloat_to_str(&a, 20); free(s); }
    double elapsed = now_seconds() - start;

    printf("  %-10s %6zu bits  %6d digits  %8d iters  %9.3f ms  %10.3f us/op\n",
           "to_str", precision, digits, iters, elapsed * 1e3, elapsed * 1e6 / iters);

    bigfloat_free(&a);
    free(sa);
}

/** Runs every benchmark. */
static void run_benchmarks(void) {
    srand(7);

    printf("== addition / subtraction ==\n");
    bench_binop("add", bignum_add, 50, 200000);
    bench_binop("add", bignum_add, 1000, 20000);
    bench_binop("sub", bignum_sub, 50, 200000);
    bench_binop("sub", bignum_sub, 1000, 20000);

    printf("== multiplication (schoolbook range, < 32 limbs) ==\n");
    bench_binop("mul", bignum_mul, 50, 20000);
    bench_binop("mul", bignum_mul, 200, 5000);

    printf("== multiplication (karatsuba range, >= 32 limbs) ==\n");
    bench_binop("mul", bignum_mul, 400, 2000);
    bench_binop("mul", bignum_mul, 1000, 500);
    bench_binop("mul", bignum_mul, 5000, 30);

    printf("== division ==\n");
    bench_divmod(50, 10, 20000);      // divisor of one limb
    bench_divmod(1000, 500, 2000);    // Knuth, divisor of several limbs
    bench_divmod(5000, 2000, 200);

    // A rational op is a multiply or add plus a divmod for the gcd reduction,
    // so the reduction dominates the time, not the bignum op itself.
    printf("== bigrational arithmetic ==\n");
    bench_rat_binop("rat add", bigrational_add, 20, 20000);
    bench_rat_binop("rat add", bigrational_add, 200, 2000);
    bench_rat_binop("rat sub", bigrational_sub, 20, 20000);
    bench_rat_binop("rat mul", bigrational_mul, 20, 20000);
    bench_rat_binop("rat mul", bigrational_mul, 200, 2000);
    bench_rat_div(20, 20000);
    bench_rat_div(200, 2000);

    printf("== bigfloat arithmetic ==\n");
    bench_bigfloat_binop("bf add", bigfloat_add, 128, 20, 20000);
    bench_bigfloat_binop("bf add", bigfloat_add, 1024, 200, 2000);
    bench_bigfloat_binop("bf mul", bigfloat_mul, 128, 20, 20000);
    bench_bigfloat_binop("bf mul", bigfloat_mul, 1024, 200, 2000);
    bench_bigfloat_binop("bf div", bigfloat_div, 128, 20, 5000);
    bench_bigfloat_binop("bf div", bigfloat_div, 1024, 200, 500);
    bench_bigfloat_to_str(128, 20, 5000);
    bench_bigfloat_to_str(1024, 200, 500);
}

/** Picks the suite from the command line (see the top of the file). */
int main(int argc, char **argv) {
    int run_correctness = 1;
    int run_bench = 0;

    if (argc > 1) {
        if (strcmp(argv[1], "bench") == 0) {
            run_correctness = 0;
            run_bench = 1;
        } else if (strcmp(argv[1], "all") == 0) {
            run_bench = 1;
        }
    }

    if (run_correctness) run_tests();
    if (run_bench) run_benchmarks();

    if (!run_correctness) return 0;

    if (failures == 0) {
        printf("all tests passed\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
