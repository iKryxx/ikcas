#include "arith.h"
#include "engine/cas.h"

#include <ctype.h>

// integer literals

/** Matches a run of decimal digits. */
static size_t int_literal_match(const char *s, size_t len)
{
    size_t n = 0;
    while (n < len && isdigit((unsigned char)s[n]))
        n++;
    return n;
}

/** Builds an int from decimal digits. */
static cas_status_t int_literal_make(cas_t *cas, const char *text, size_t len, value_t *out)
{
    out->type = &arith_type_int;
    bignum_init(&out->as.i, allocator_heap());
    if (bignum_from_str(&out->as.i, text, len, 10) != 0) {
        bignum_destroy(&out->as.i);
        return cas_error(cas, CAS_ERR_SYNTAX, "invalid number '%.*s'", (int)len, text);
    }
    return CAS_OK;
}

static const literal_def_t int_literal = { "int", int_literal_match, int_literal_make };

// functions

/**
 * Applies op left to right over all args, so variadic add and mul behave like
 * a chain of the binary operator.
 */
static cas_status_t fold_arith(cas_t *cas, value_op_t op, const value_t *args, size_t n, value_t *out)
{
    value_t acc;
    cas_status_t st = value_copy(cas, &acc, &args[0]);
    for (size_t i = 1; st == CAS_OK && i < n; i++) {
        value_t next;
        st = value_arith(cas, op, &next, &acc, &args[i]);
        value_destroy(&acc);
        if (st == CAS_OK)
            acc = next;
    }
    if (st == CAS_OK)
        *out = acc;
    return st;
}

/** add(a, b, ...). */
static cas_status_t fn_add_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_ADD, args, n, out);
}

/** sub(a, b). */
static cas_status_t fn_sub_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_SUB, args, n, out);
}

/** mul(a, b, ...). */
static cas_status_t fn_mul_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_MUL, args, n, out);
}

/** div(a, b). */
static cas_status_t fn_div_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    return fold_arith(cas, VOP_DIV, args, n, out);
}

/** neg(a). */
static cas_status_t fn_neg_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    return value_negate(cas, out, &args[0]);
}

/** float(a): converts to a float. */
static cas_status_t fn_float_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    return value_convert(cas, out, &args[0], &arith_type_float);
}

static const fn_def_t fn_add = { "add", 2, FN_VARIADIC, FN_COMMUTATIVE | FN_ASSOCIATIVE, fn_add_eval, NULL };
static const fn_def_t fn_sub = { "sub", 2, 2, 0, fn_sub_eval, NULL };
static const fn_def_t fn_mul = { "mul", 2, FN_VARIADIC, FN_COMMUTATIVE | FN_ASSOCIATIVE, fn_mul_eval, NULL };
static const fn_def_t fn_div = { "div", 2, 2, 0, fn_div_eval, NULL };
static const fn_def_t fn_neg = { "neg", 1, 1, 0, fn_neg_eval, NULL };
static const fn_def_t fn_float = { "float", 1, 1, 0, fn_float_eval, NULL };

// syntax

static const op_def_t ops[] = {
    { "+", FIX_INFIX, 10, ASSOC_LEFT, &fn_add },
    { "-", FIX_INFIX, 10, ASSOC_LEFT, &fn_sub },
    { "*", FIX_INFIX, 20, ASSOC_LEFT, &fn_mul },
    { "/", FIX_INFIX, 20, ASSOC_LEFT, &fn_div },
    { "-", FIX_PREFIX, 30, ASSOC_LEFT, &fn_neg },
};

static const value_type_t *const types[] = { &arith_type_int, &arith_type_rational, &arith_type_float };
static const fn_def_t *const fns[] = { &fn_add, &fn_sub, &fn_mul, &fn_div, &fn_neg, &fn_float };

/** Registers the numeric types, the integer literal, the functions and their operators. */
static cas_status_t arith_init(cas_t *cas)
{
    cas_status_t st = CAS_OK;
    for (size_t i = 0; st == CAS_OK && i < sizeof(types) / sizeof(*types); i++)
        st = registry_add_type(cas, types[i]);
    if (st == CAS_OK)
        st = registry_add_literal(cas, &int_literal);
    for (size_t i = 0; st == CAS_OK && i < sizeof(fns) / sizeof(*fns); i++)
        st = registry_add_fn(cas, fns[i]);
    for (size_t i = 0; st == CAS_OK && i < sizeof(ops) / sizeof(*ops); i++)
        st = registry_add_op(cas, &ops[i]);
    return st;
}

IKCAS_PLUGIN(arith, "1.0", arith_init, NULL);
