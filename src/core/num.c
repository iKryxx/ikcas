//
// Created by ikryxxdev on 2/10/26.
//

#include "core/num.h"

#include <stdio.h>

static int64_t i64_abs(int64_t x) {
    return x < 0 ? -x : x;
}

static int64_t i64_gcd(int64_t a, int64_t b) {
    a = i64_abs(a); b = i64_abs(b);
    while (b) {
        int64_t t = a % b; a = b; b = t;
    }
    return a ? a : 0;
}

rat_t rat_from_i64(int64_t num) {
    return (rat_t){num, 1};
}

bool rat_is_zero(rat_t rat) {
    return rat.num == 0;
}

rat_t rat_norm(rat_t a) {
    if (a.den == 0) return a;
    if (a.num == 0) return (rat_t){0, 1};
    if (a.den < 0) return (rat_t){-a.num, -a.den};

    int64_t gcd = i64_gcd(a.num, a.den);
    return (rat_t){a.num / gcd, a.den / gcd};
}

rat_t rat_add(rat_t a, rat_t b) {
    __int128_t num = (__int128_t)a.num * b.den + (__int128_t)b.num * a.den;
    __int128_t den = (__int128_t)a.den * b.den;
    rat_t r = (rat_t){(int64_t)num, (int64_t)den};
    return rat_norm(r);
}

rat_t rat_sub(rat_t a, rat_t b) {
    b.num = -b.num;
    return rat_add(a, b);
}

rat_t rat_mul(rat_t a, rat_t b) {
    __int128_t num = (__int128_t)a.num * b.num;
    __int128_t den = (__int128_t)a.den * b.den;
    rat_t r = (rat_t){(int64_t)num, (int64_t)den};
    return rat_norm(r);
}

rat_t rat_div(rat_t a, rat_t b, bool *ok) {
    if (b.num == 0) { if (ok) *ok = false; return rat_from_i64(0); }
    __int128_t num = (__int128_t)a.num * b.den;
    __int128_t den = (__int128_t)a.den * b.num;
    rat_t r = (rat_t){(int64_t)num, (int64_t)den};
    if (ok) *ok = true;
    return rat_norm(r);
}

rat_t rat_pow_i64(rat_t a, int64_t e, bool *ok) {
    if (ok) *ok = true;
    a = rat_norm(a);

    if (e == 0) return rat_from_i64(1);
    if (a.num == 0 && e < 0) { if (ok) *ok = false; return rat_from_i64(0); }

    bool neg = e < 0;
    uint64_t p = (uint64_t)(neg ? -e : e);

    rat_t base = a;
    rat_t res = rat_from_i64(1);
    while (p) {
        if (p & 1) res = rat_mul(res, base);
        base = rat_mul(base, base);
        p >>= 1;
    }

    if (neg) {
        bool divok = true;
        res = rat_div(rat_from_i64(1), res, &divok);
        if (ok) *ok = divok;
    }

    return res;
}

void rat_to_str(rat_t rat, char *buf, int cap) {
    rat = rat_norm(rat);
    if (rat.den == 1) snprintf(buf, (size_t)cap, "%lld", (long long)rat.num);
    else snprintf(buf, (size_t)cap, "%lld/%lld", (long long)rat.num, (long long)rat.den);
}

