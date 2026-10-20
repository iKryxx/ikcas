#include "arith/arith.h"
#include "engine/cas.h"

// Example plugin on top of arith: a right-associative '^' and abs(). List
// "extra_ops" in plugins.conf to build it in, or run `make plugin-extra_ops`
// and load the .so with `ikcas -p`.

/** Reads the exponent's magnitude into out. Returns 0 if it doesn't fit in 64 bits. */
static int exponent_u64(const bignum_t *e, uint64_t *out)
{
    if (e->len > 2)
        return 0;
    uint64_t v = e->limb[0];
    if (e->len == 2)
        v |= (uint64_t)e->limb[1] << 32;
    *out = v;
    return 1;
}

/**
 * Computes base^exp exactly for an int or rational base and an int exponent.
 * A negative exponent inverts the base first.
 */
static cas_status_t fn_pow_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    const value_t *base = &args[0], *exp = &args[1];
    if (!arith_is_int(exp) || arith_is_float(base))
        return cas_error(cas, CAS_ERR_TYPE, "^ needs an int or rational base and an int exponent");

    uint64_t e;
    if (!exponent_u64(&exp->as.i, &e))
        return cas_error(cas, CAS_ERR_DOMAIN, "exponent too large");

    bignum_t num, den, rn, rd;
    bignum_init(&num, allocator_heap());
    bignum_init(&den, allocator_heap());
    bignum_init(&rn, allocator_heap());
    bignum_init(&rd, allocator_heap());

    bignum_copy(&num, arith_is_int(base) ? &base->as.i : &base->as.q.num);
    if (arith_is_int(base))
        bignum_from_i64(&den, 1);
    else
        bignum_copy(&den, &base->as.q.denom);

    cas_status_t st = CAS_OK;
    if (exp->as.i.sign < 0 && e != 0) {
        if (bignum_is_zero(&num))
            st = cas_error(cas, CAS_ERR_DOMAIN, "zero to a negative power");
        else {
            bignum_t tmp = num;
            num = den;
            den = tmp;
        }
    }
    if (st == CAS_OK) {
        bignum_pow_u64(&rn, &num, e);
        bignum_pow_u64(&rd, &den, e);
        st = arith_make_rational(cas, out, &rn, &rd);
    }

    bignum_destroy(&num);
    bignum_destroy(&den);
    bignum_destroy(&rn);
    bignum_destroy(&rd);
    return st;
}

/** Absolute value of any arith number. */
static cas_status_t fn_abs_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    if (arith_sign(&args[0]) < 0)
        return value_negate(cas, out, &args[0]);
    return value_copy(cas, out, &args[0]);
}

static const fn_def_t fn_pow = { "pow", 2, 2, 0, fn_pow_eval, NULL };
static const fn_def_t fn_abs = { "abs", 1, 1, 0, fn_abs_eval, NULL };
// Binds tighter than prefix minus (30), so -2^2 is -(2^2).
static const op_def_t op_pow = { "^", FIX_INFIX, 40, ASSOC_RIGHT, &fn_pow };

/** Registers pow, abs and the ^ operator. */
static cas_status_t extra_ops_init(cas_t *cas)
{
    cas_status_t st = registry_add_fn(cas, &fn_pow);
    if (st == CAS_OK)
        st = registry_add_fn(cas, &fn_abs);
    if (st == CAS_OK)
        st = registry_add_op(cas, &op_pow);
    return st;
}

IKCAS_PLUGIN(extra_ops, "1.0", extra_ops_init, NULL);
