#include "core/bignum.h"
#include "core/bignum_internal.h"

#include <stdlib.h>
#include <string.h>

// Shared constant 1, handy for comparisons.
const bignum_t bignum_one = {(uint32_t[1]){1}, 1, 1, 1, {NULL, NULL}};

/** Grows the limb array to at least cap limbs. Aborts if the allocation fails. */
static void ensure_cap(bignum_t *n, size_t cap) {
    if (n->cap >= cap) return;
    size_t new_cap = n->cap ? n->cap * 2 : 4;

    if (new_cap < cap) new_cap = cap;
    uint32_t *p = allocator_realloc(n->alloc, n->limb,
                                    n->cap * sizeof(uint32_t),
                                    new_cap * sizeof(uint32_t));
    if (!p) abort(); // TODO: handle allocation failure
    n->limb = p;
    n->cap = new_cap;
}

/** Drops leading zero limbs and makes zero positive. */
static void normalize(bignum_t *n) {
    while (n->len > 1 && n->limb[n->len - 1] == 0) n->len--;
    if (n->len == 1 && n->limb[0] == 0) n->sign = 1;
}

// Raw limb primitives. Declared in bignum_internal.h and shared with bigfloat.c.

/** Compares two magnitudes. Returns -1, 0 or 1. */
int cmp_mag_raw(const uint32_t *a, size_t alen, const uint32_t *b, size_t blen) {
    if (alen != blen) return alen < blen ? -1 : 1;
    for (size_t i = alen; i-- > 0; ) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

/**
 * dst = a + b on magnitudes. dst needs max(alen, blen) + 1 limbs. Returns the
 * result length.
 */
size_t add_mag(uint32_t *dst, const uint32_t *a, size_t alen, const uint32_t *b, size_t blen) {
    if (alen < blen) {
        const uint32_t *t = a;
        a = b;
        b = t;
        size_t tlen = alen;
        alen = blen;
        blen = tlen;
    }

    uint64_t carry = 0;
    size_t i = 0;
    for (; i < blen; i++) {
        uint64_t s = (uint64_t)a[i] + b[i] + carry;
        dst[i] = (uint32_t)s;
        carry = s >> 32;
    }
    for (; i < alen; i++) {
        uint64_t s = (uint64_t)a[i] + carry;
        dst[i] = (uint32_t)s;
        carry = s >> 32;
    }
    if (carry) dst[i++] = (uint32_t)carry;
    return i;
}

/**
 * dst = a - b on magnitudes, with a >= b and alen >= blen. dst needs alen limbs.
 * Returns the length with leading zeros trimmed.
 */
size_t sub_mag(uint32_t *dst, const uint32_t *a, size_t alen, const uint32_t *b, size_t blen) {
    int64_t borrow = 0;
    size_t i = 0;
    for (; i < blen; i++) {
        int64_t d = (int64_t)a[i] - b[i] - borrow;
        if (d < 0) {
            d += ((int64_t)1 << 32);
            borrow = 1;
        } else {
            borrow = 0;
        }
        dst[i] = (uint32_t)d;
    }
    for (; i < alen; i++) {
        int64_t d = (int64_t)a[i] - borrow;
        if (d < 0) {
            d += ((int64_t)1 << 32);
            borrow = 1;
        } else {
            borrow = 0;
        }
        dst[i] = (uint32_t)d;
    }
    size_t len = alen;
    while (len > 1 && dst[len - 1] == 0) len--;
    return len;
}

// bignum_to_str needs this before its definition.
static uint32_t divmod_small(uint32_t *q, const uint32_t *u, size_t ulen, uint32_t d);

/** Sets n to zero and makes it use alloc for its limbs. */
void bignum_init(bignum_t *n, allocator_t alloc) {
    n->alloc = alloc;
    n->limb = NULL;
    n->len = 1;
    n->cap = 0;
    n->sign = 1;
    ensure_cap(n, 1);
    n->limb[0] = 0;
}

/** Releases the limbs. */
void bignum_destroy(bignum_t *n) {
    allocator_free(n->alloc, n->limb, n->cap * sizeof(uint32_t));
    n->limb = NULL;
    n->len = 0;
    n->cap = 0;
}

/** dst = src. dst has to be initialized. */
void bignum_copy(bignum_t *dst, const bignum_t *src) {
    ensure_cap(dst, src->len);
    memcpy(dst->limb, src->limb, src->len * sizeof(uint32_t));
    dst->len = src->len;
    dst->sign = src->sign;
}

/**
 * Sets n from the 64 bits of v read as unsigned. The parameter is int64_t, but
 * only the bit pattern matters.
 */
void bignum_from_u64(bignum_t *n, int64_t v) {
    ensure_cap(n, 2);
    n->limb[0] = (uint32_t)v;
    n->limb[1] = (uint32_t)(v >> 32);
    n->len = n->limb[1] ? 2 : 1;
    n->sign = 1;
}

/** Sets n to the signed value v. INT64_MIN works. */
void bignum_from_i64(bignum_t *n, int64_t v) {
    uint64_t mag = v < 0 ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    bignum_from_u64(n, mag);
    n->sign = (v < 0) ? -1 : 1;
}

/** Maps a character to its digit value, 0xFF for anything invalid. Built on first use. */
static const unsigned char *digit_table(void) {
    static unsigned char table[256];
    static int initialized = 0;
    if (!initialized) {
        memset(table, 0xFF, sizeof(table));
        for (int i = 0; i < 10; i++) table[(unsigned char)('0' + i)] = (unsigned char)i;
        for (int i = 0; i < 26; i++) {
            table[(unsigned char)('a' + i)] = (unsigned char)(10 + i);
            table[(unsigned char)('A' + i)] = (unsigned char)(10 + i);
        }
        initialized = 1;
    }
    return table;
}

/**
 * Finds the largest k with base^k below 2^32, plus that power. from_str and
 * to_str use it to handle k digits per pass over the limbs instead of one.
 */
static void chunk_params_for_base(int base, int *k_out, uint32_t *pow_out) {
    uint32_t pow = 1;
    int k = 0;
    for (;;) {
        uint64_t next = (uint64_t)pow * (uint32_t)base;
        if (next > 0xFFFFFFFFu) break;
        pow = (uint32_t)next;
        k++;
    }
    *k_out = k;
    *pow_out = pow;
}

/** Parses len chars in the given base, with optional leading signs. Returns -1 on bad input. */
int bignum_from_str(bignum_t *n, const char *str, size_t len, int base) {
    size_t cur_len = 0;
    if (base < 2 || base > 36) return -1;
    int sign = 1;
    while (*str == '+' || *str == '-') {
        if (*str == '-') sign *= -1;
        str++;
        cur_len++;
        if (cur_len == len) return -1; // a sign with no digits
    }
    if (*str == '\0') return -1;

    const unsigned char *table = digit_table();

    n->len = 1;
    if (n->cap == 0) ensure_cap(n, 1);
    n->limb[0] = 0;

    int k;
    uint32_t base_pow_k;
    chunk_params_for_base(base, &k, &base_pow_k);

    size_t total_len = len - cur_len;
    size_t pos = 0;
    while (pos < total_len) {
        size_t remaining = total_len - pos;
        size_t chunk_chars = remaining < (size_t)k ? remaining : (size_t)k;

        uint32_t chunk_val = 0;
        uint32_t chunk_pow = 1;
        for (size_t i = 0; i < chunk_chars; i++) {
            unsigned char d = table[(unsigned char)str[pos + i]];
            if (d == 0xFF || d >= (unsigned)base) return -1;
            chunk_val = chunk_val * (uint32_t)base + d;
            chunk_pow *= (uint32_t)base;
        }
        pos += chunk_chars;

        // n = n * chunk_pow + chunk_val, one pass over the limbs
        uint64_t carry = chunk_val;
        for (size_t i = 0; i < n->len; i++) {
            uint64_t v = (uint64_t)n->limb[i] * chunk_pow + carry;
            n->limb[i] = (uint32_t)v;
            carry = v >> 32;
        }
        if (carry) {
            ensure_cap(n, n->len + 1);
            n->limb[n->len++] = (uint32_t)carry;
        }
    }

    n->sign = sign;
    normalize(n);
    return 0;
}

/** bignum_from_str for a NUL-terminated string. */
int bignum_from_cstr(bignum_t *n, const char *str, int base) {
    return bignum_from_str(n, str, strlen(str), base);
}

/** Digit value to lowercase character. */
static char digit_to_char(uint32_t d) {
    return (char)(d < 10 ? '0' + d : 'a' + d - 10);
}

/** floor(log2(x)) for x > 0. */
static int floor_log2_u32(unsigned x) {
    int pos = 0;
    while (x >>= 1) pos++;
    return pos;
}

/**
 * Upper bound on the digits needed to print len limbs in the given base. It uses
 * floor(log2(base)), which is never above the real value, so the bound can't
 * come out too small. Integer math only, no libm.
 */
static size_t to_str_capacity(size_t len, int base) {
    size_t bits = len * 32;
    int lb = floor_log2_u32((unsigned)base);
    if (lb < 1) lb = 1;
    return bits / (size_t)lb + 2;
}

/**
 * Converts to a malloc'd string. Returns NULL for a bad base or when out of
 * memory. Divides by base^k repeatedly, writes the digits least significant
 * first, then reverses them.
 */
char *bignum_to_str(const bignum_t *n, int base) {
    if (base < 2 || base > 36) return NULL;

    if (bignum_is_zero(n)) {
        char *s = malloc(2);
        if (!s) return NULL;
        s[0] = '0';
        s[1] = '\0';
        return s;
    }

    size_t len = n->len;
    uint32_t *scratch = malloc(len * sizeof(uint32_t));
    if (!scratch) return NULL;
    memcpy(scratch, n->limb, len * sizeof(uint32_t));

    size_t cap = to_str_capacity(len, base);
    char *buf = malloc(cap + 2);
    if (!buf) { free(scratch); return NULL; }

    int k;
    uint32_t base_pow_k;
    chunk_params_for_base(base, &k, &base_pow_k);

    size_t pos = 0;
    while (len > 0 && !(len == 1 && scratch[0] == 0)) {
        uint32_t rem = divmod_small(scratch, scratch, len, base_pow_k);
        while (len > 1 && scratch[len - 1] == 0) len--;
        int done = (len == 1 && scratch[0] == 0);

        if (!done) {
            for (int i = 0; i < k; i++) {
                buf[pos++] = digit_to_char(rem % (uint32_t)base);
                rem /= (uint32_t)base;
            }
        } else {
            while (rem > 0) {
                buf[pos++] = digit_to_char(rem % (uint32_t)base);
                rem /= (uint32_t)base;
            }
        }
    }

    if (n->sign < 0) buf[pos++] = '-';

    free(scratch);

    char *out = malloc(pos + 2);
    if (!out) { free(buf); return NULL; }
    size_t o = 0;
    if (n->sign < 0) out[o++] = '-';
    size_t digit_count = (n->sign < 0) ? pos - 1 : pos;
    for (size_t i = 0; i < digit_count; i++) {
        out[o++] = buf[digit_count - 1 - i];
    }
    out[o] = '\0';
    free(buf);
    return out;
}

/** True if n is zero. */
int bignum_is_zero(const bignum_t *n) {
    return n->len == 1 && n->limb[0] == 0;
}

/** Compares signed values. Returns -1, 0 or 1. */
int bignum_cmp(const bignum_t *a, const bignum_t *b) {
    if (bignum_is_zero(a) && bignum_is_zero(b)) return 0;
    if(a->sign != b->sign) return a->sign < b->sign ? -1 : 1;
    int mc = bignum_cmp_mag(a, b);
    return a->sign > 0 ? mc : -mc;
}

/** Compares absolute values. */
int bignum_cmp_mag(const bignum_t *a, const bignum_t *b) {
    return cmp_mag_raw(a->limb, a->len, b->limb, b->len);
}

/** Flips the sign in place. Zero stays positive. */
void bignum_negate(bignum_t *n) {
    if (!bignum_is_zero(n)) n->sign = -n->sign;
}

/** True if the lowest bit is clear. */
int bignum_is_even(const bignum_t *n) {
    return !(n->limb[0] & 1u);
}

/** dst = n / 2, rounded toward zero. */
void bignum_shr1(bignum_t *dst, const bignum_t *n) {
    size_t len = n->len;
    ensure_cap(dst, len);
    uint32_t carry = 0;
    for (size_t i = len; i-- > 0; ) {
        uint32_t v = n->limb[i];
        dst->limb[i] = (v >> 1) | (carry << 31);
        carry = v & 1u;
    }
    dst->len = len;
    dst->sign = n->sign;
    normalize(dst);
}

/**
 * Computes a + b, with b's sign taken from bsign (bignum_sub passes the flipped
 * sign). If dst is neither a nor b, the result goes straight into dst.
 * Otherwise it goes through a scratch buffer, because growing dst could
 * overwrite an operand that is still being read.
 */
static void add_signed(bignum_t *dst, const bignum_t *a, const bignum_t *b, int bsign) {
    const uint32_t *al = a->limb, *bl = b->limb;
    size_t alen = a->len, blen = b->len;
    int asign = a->sign;
    size_t max_len = (alen > blen ? alen : blen) + 1;

    if (dst != a && dst != b) {
        ensure_cap(dst, max_len);
        if (asign == bsign) {
            size_t len = add_mag(dst->limb, al, alen, bl, blen);
            dst->len = len;
            dst->sign = asign;
        } else {
            int cmp = cmp_mag_raw(al, alen, bl, blen);
            size_t len;
            int sign;
            if (cmp == 0) {
                len = 1;
                dst->limb[0] = 0;
                sign = 1;
            } else if (cmp > 0) {
                len = sub_mag(dst->limb, al, alen, bl, blen);
                sign = asign;
            } else {
                len = sub_mag(dst->limb, bl, blen, al, alen);
                sign = bsign;
            }
            dst->len = len;
            dst->sign = sign;
        }
        normalize(dst);
        return;
    }

    if (asign == bsign) {
        uint32_t *tmp = malloc(max_len * sizeof(uint32_t));
        if (!tmp) abort(); // TODO: handle allocation failure

        size_t len = add_mag(tmp, al, alen, bl, blen);
        ensure_cap(dst, len);

        memcpy(dst->limb, tmp, len * sizeof(uint32_t));
        dst->len = len;
        dst->sign = asign;

        free(tmp);
    } else {
        int cmp = cmp_mag_raw(al, alen, bl, blen);
        uint32_t *tmp = malloc((alen > blen ? alen : blen) * sizeof(uint32_t));

        if (!tmp) abort(); // TODO: handle allocation failure
        size_t len;
        int sign;
        if (cmp == 0) {
            len = 1;
            tmp[0] = 0;
            sign = 1;
        } else if (cmp > 0) {
            len = sub_mag(tmp, al, alen, bl, blen);
            sign = asign;
        } else {
            len = sub_mag(tmp, bl, blen, al, alen);
            sign = bsign;
        }
        ensure_cap(dst, len);
        memcpy(dst->limb, tmp, len * sizeof(uint32_t));
        dst->len = len;
        dst->sign = sign;
        free(tmp);
    }
    normalize(dst);
}

/** dst = a + b. */
void bignum_add(bignum_t *dst, const bignum_t *a, const bignum_t *b) {
    add_signed(dst, a, b, b->sign);
}

/** dst = a - b. */
void bignum_sub(bignum_t *dst, const bignum_t *a, const bignum_t *b) {
    int bsign = bignum_is_zero(b) ? 1 : -b->sign;
    add_signed(dst, a, b, bsign);
}

// Below this many limbs schoolbook is faster: Karatsuba's extra additions
// and recursion cost more than they save.
#define KARATSUBA_THRESHOLD 32

/** O(an * bn) multiplication. dst must be zeroed and hold an + bn limbs. */
static void mul_schoolbook(uint32_t *dst, const uint32_t *al, size_t an, const uint32_t *bl, size_t bn) {
    for (size_t i = 0; i < an; i++) {
        uint64_t ai = al[i];
        if (ai == 0) continue;

        uint64_t carry = 0;
        for (size_t j = 0; j < bn; j++) {
            uint64_t v = ai * (uint64_t)bl[j] + dst[i + j] + carry;
            dst[i + j] = (uint32_t)v;
            carry = v >> 32;
        }
        size_t k = i + bn;
        while (carry) {
            uint64_t v = (uint64_t)dst[k] + carry;
            dst[k] = (uint32_t)v;
            carry = v >> 32;
            k++;
        }
    }
}

/** Adds src into dst starting at limb offset, carrying upward. Anything past dstlen is dropped. */
static void add_into(uint32_t *dst, size_t dstlen, const uint32_t *src, size_t srclen, size_t offset) {
    if (offset >= dstlen) return;
    size_t room = dstlen - offset;
    if (srclen > room) srclen = room;

    uint64_t carry = 0;
    size_t i = 0;
    for (; i < srclen; i++) {
        uint64_t s = (uint64_t)dst[offset + i] + src[i] + carry;
        dst[offset + i] = (uint32_t)s;
        carry = s >> 32;
    }
    for (; carry && offset + i < dstlen; i++) {
        uint64_t s = (uint64_t)dst[offset + i] + carry;
        dst[offset + i] = (uint32_t)s;
        carry = s >> 32;
    }
}

/** Length of arr without leading zero limbs, at least 1. */
static size_t trim_len(const uint32_t *arr, size_t len) {
    while (len > 1 && arr[len - 1] == 0) len--;
    return len;
}

/**
 * Splits both operands at half and makes three multiplications instead of four:
 * a0*b0, a1*b1 and (a0+a1)(b0+b1), from which the middle term follows.
 */
static void mul_karatsuba(uint32_t *dst, const uint32_t *al, size_t an, const uint32_t *bl, size_t bn) {
    size_t n = an > bn ? an : bn;
    size_t half = (n + 1) / 2;

    size_t alo = an < half ? an : half;
    size_t blo = bn < half ? bn : half;
    size_t ahi = an > half ? an - half : 0;
    size_t bhi = bn > half ? bn - half : 0;

    const uint32_t *a0 = al, *a1 = al + half;
    const uint32_t *b0 = bl, *b1 = bl + half;

    size_t z0len = alo + blo;
    size_t z2len = ahi + bhi;

    uint32_t *z0 = malloc((z0len ? z0len : 1) * sizeof(uint32_t));
    uint32_t *z2 = malloc((z2len ? z2len : 1) * sizeof(uint32_t));
    if (!z0 || !z2) abort(); // TODO: handle allocation failure
    mul_mag(z0, a0, alo, b0, blo);
    mul_mag(z2, a1, ahi, b1, bhi);

    size_t sumA_cap = (alo > ahi ? alo : ahi) + 1;
    size_t sumB_cap = (blo > bhi ? blo : bhi) + 1;
    uint32_t *sumA = malloc(sumA_cap * sizeof(uint32_t));
    uint32_t *sumB = malloc(sumB_cap * sizeof(uint32_t));
    if (!sumA || !sumB) abort(); // TODO: handle allocation failure
    size_t sumA_len = add_mag(sumA, a0, alo, a1, ahi);
    size_t sumB_len = add_mag(sumB, b0, blo, b1, bhi);

    size_t zmidlen = sumA_len + sumB_len;
    uint32_t *zmid = malloc((zmidlen ? zmidlen : 1) * sizeof(uint32_t));
    if (!zmid) abort(); // TODO: handle allocation failure
    mul_mag(zmid, sumA, sumA_len, sumB, sumB_len);

    // z1 = zmid - z0 - z2 = (a0+a1)(b0+b1) - a0*b0 - a1*b1.
    // sub_mag wants the minuend's array length >= the subtrahend's. That only
    // holds for trimmed lengths. The raw split lengths don't guarantee it,
    // even though the values do satisfy zmid >= z0 and zmid - z0 >= z2.
    size_t z1len = sub_mag(zmid, zmid, trim_len(zmid, zmidlen), z0, trim_len(z0, z0len));
    z1len = sub_mag(zmid, zmid, z1len, z2, trim_len(z2, z2len));

    add_into(dst, an + bn, z0, z0len, 0);
    add_into(dst, an + bn, zmid, z1len, half);
    add_into(dst, an + bn, z2, z2len, 2 * half);

    free(z0);
    free(z2);
    free(sumA);
    free(sumB);
    free(zmid);
}

/** dst = a * b on magnitudes. dst gets zeroed first and must hold an + bn limbs. */
void mul_mag(uint32_t *dst, const uint32_t *al, size_t an, const uint32_t *bl, size_t bn) {
    memset(dst, 0, (an + bn) * sizeof(uint32_t));
    if (an == 0 || bn == 0) return;

    if (an < KARATSUBA_THRESHOLD || bn < KARATSUBA_THRESHOLD) {
        mul_schoolbook(dst, al, an, bl, bn);
    } else {
        mul_karatsuba(dst, al, an, bl, bn);
    }
}

/** dst = a * b. */
void bignum_mul(bignum_t *dst, const bignum_t *a, const bignum_t *b) {
    if (bignum_is_zero(a) || bignum_is_zero(b)) {
        ensure_cap(dst, 1);
        dst->limb[0] = 0;
        dst->len = 1;
        dst->sign = 1;
        return;
    }

    size_t rlen = a->len + b->len;
    uint32_t *acc = malloc(rlen * sizeof(uint32_t));
    if (!acc) abort(); // TODO: handle allocation failure

    mul_mag(acc, a->limb, a->len, b->limb, b->len);

    ensure_cap(dst, rlen);
    memcpy(dst->limb, acc, rlen * sizeof(uint32_t));
    dst->len = rlen;
    dst->sign = a->sign * b->sign;
    free(acc);
    normalize(dst);
}

/** dst = base^exp by square-and-multiply. */
void bignum_pow_u64(bignum_t *dst, const bignum_t *base, uint64_t exp) {
    if (exp == 0) {
        ensure_cap(dst, 1);
        dst->limb[0] = 1;
        dst->len = 1;
        dst->sign = 1;
        return;
    }

    bignum_t result, b;
    bignum_init(&result, allocator_heap());
    bignum_init(&b, allocator_heap());
    bignum_from_i64(&result, 1);
    bignum_copy(&b, base);

    while (exp > 0) {
        if (exp & 1) bignum_mul(&result, &result, &b);
        exp >>= 1;
        if (exp) bignum_mul(&b, &b, &b);
    }

    bignum_copy(dst, &result);
    bignum_destroy(&result);
    bignum_destroy(&b);
}

/** Divides u by a single limb d into q (may alias u). Returns the remainder. */
static uint32_t divmod_small(uint32_t *q, const uint32_t *u, size_t ulen, uint32_t d) {
    uint64_t rem = 0;
    for (size_t i = ulen; i-- > 0; ) {
        uint64_t cur = (rem << 32) | u[i];
        q[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    return (uint32_t)rem;
}

/**
 * Long division after Knuth's algorithm D, for a divisor of 2 or more limbs.
 * q gets ulen - vlen + 1 limbs and r gets vlen limbs. The divisor is shifted
 * left first so its top bit is set, which keeps the quotient estimates tight.
 */
static void divmod_knuth(uint32_t *q, uint32_t *r, const uint32_t *u, size_t ulen, const uint32_t *v, size_t vlen) {
    int shift = 0;
    uint32_t top = v[vlen - 1];
    while (!(top & 0x80000000u)) { top <<= 1; shift++; }

    uint32_t *vn = malloc(vlen * sizeof(uint32_t));
    uint32_t *un = malloc((ulen + 1) * sizeof(uint32_t));
    if (!vn || !un) abort();

    if (shift) {
        for (size_t i = vlen - 1; i > 0; i--)
            vn[i] = (v[i] << shift) | (v[i - 1] >> (32 - shift));
        vn[0] = v[0] << shift;
        un[ulen] = u[ulen - 1] >> (32 - shift);
        for (size_t i = ulen - 1; i > 0; i--)
            un[i] = (u[i] << shift) | (u[i - 1] >> (32 - shift));
        un[0] = u[0] << shift;
    } else {
        memcpy(vn, v, vlen * sizeof(uint32_t));
        memcpy(un, u, ulen * sizeof(uint32_t));
        un[ulen] = 0;
    }

    size_t qlen = ulen - vlen + 1;
    for (size_t j = qlen; j-- > 0; ) {
        // Estimate the quotient digit from the top two limbs of the remaining
        // dividend and the top limb of the divisor.
        uint64_t num = ((uint64_t)un[j + vlen] << 32) | un[j + vlen - 1];
        uint64_t qhat = num / vn[vlen - 1];
        uint64_t rhat = num % vn[vlen - 1];

        if (qhat > 0xFFFFFFFFu) qhat = 0xFFFFFFFFu;

        // Correct the estimate. Knuth shows two adjustments are enough.
        while (vlen >= 2 && qhat * vn[vlen - 2] > (rhat << 32 | un[j + vlen - 2])) {
            qhat--;
            rhat += vn[vlen - 1];
            if (rhat > 0xFFFFFFFFu) break;
        }

        // Subtract qhat * vn from un[j..j+vlen]
        int64_t borrow = 0;
        uint64_t carry = 0;
        for (size_t i = 0; i < vlen; i++) {
            uint64_t p = qhat * vn[i] + carry;
            carry = p >> 32;
            int64_t sub = (int64_t)un[j + i] - (int64_t)(uint32_t)p - borrow;
            if (sub < 0) { sub += ((int64_t)1 << 32); borrow = 1; } else { borrow = 0; }
            un[j + i] = (uint32_t)sub;
        }
        int64_t sub = (int64_t)un[j + vlen] - (int64_t)carry - borrow;
        if (sub < 0) {
            // qhat was one too big: add vn back once and lower qhat
            sub += ((int64_t)1 << 32);
            un[j + vlen] = (uint32_t)sub;
            qhat--;
            uint64_t c = 0;
            for (size_t i = 0; i < vlen; i++) {
                uint64_t s = (uint64_t)un[j + i] + vn[i] + c;
                un[j + i] = (uint32_t)s;
                c = s >> 32;
            }
            un[j + vlen] = (uint32_t)((uint64_t)un[j + vlen] + c);
        } else {
            un[j + vlen] = (uint32_t)sub;
        }
        q[j] = (uint32_t)qhat;
    }

    // The remainder is un[0..vlen), shifted back down.
    if (shift) {
        for (size_t i = 0; i < vlen; i++)
            r[i] = (un[i] >> shift) | (i + 1 < vlen ? (un[i + 1] << (32 - shift)) : 0);
    } else {
        memcpy(r, un, vlen * sizeof(uint32_t));
    }

    free(vn);
    free(un);
}

/**
 * q = a / b and r = a % b, truncated toward zero, with r taking a's sign. Either
 * output can be NULL. b must not be zero.
 */
void bignum_divmod(bignum_t *q, bignum_t *r, const bignum_t *a, const bignum_t *b) {
    if (bignum_is_zero(a)) {
        if (q) { ensure_cap(q, 1); q->limb[0] = 0; q->len = 1; q->sign = 1; }
        if (r) { ensure_cap(r, 1); r->limb[0] = 0; r->len = 1; r->sign = 1; }
        return;
    }

    int cmp = cmp_mag_raw(a->limb, a->len, b->limb, b->len);
    if (cmp < 0) {
        if (q) { ensure_cap(q, 1); q->limb[0] = 0; q->len = 1; q->sign = 1; }
        if (r) { bignum_copy(r, a); }
        return;
    }

    uint32_t *qbuf = malloc(a->len * sizeof(uint32_t));
    uint32_t *rbuf = malloc((b->len > a->len ? b->len : a->len) * sizeof(uint32_t));
    size_t qlen, rlen;
    if (b->len == 1) {
        qlen = a->len;
        uint32_t rem = divmod_small(qbuf, a->limb, a->len, b->limb[0]);
        rbuf[0] = rem;
        rlen = 1;
    } else {
        qlen = a->len - b->len + 1;
        divmod_knuth(qbuf, rbuf, a->limb, a->len, b->limb, b->len);
        rlen = b->len;
    }

    while (qlen > 1 && qbuf[qlen - 1] == 0) qlen--;
    while (rlen > 1 && rbuf[rlen - 1] == 0) rlen--;

    if (q) {
        ensure_cap(q, qlen);
        memcpy(q->limb, qbuf, qlen * sizeof(uint32_t));
        q->len = qlen;
        q->sign = a->sign * b->sign;
        normalize(q);
    }
    if (r) {
        ensure_cap(r, rlen);
        memcpy(r->limb, rbuf, rlen * sizeof(uint32_t));
        r->len = rlen;
        r->sign = a->sign;
        normalize(r);
    }

    free(qbuf);
    free(rbuf);
}
