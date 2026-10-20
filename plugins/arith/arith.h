#ifndef ARITH_H
#define ARITH_H

#include "engine/plugin.h"
#include "expr/value.h"

// The arithmetic plugin: the numeric tower int < rational < float, integer
// literals, + - * / and prefix -, and float(). Other plugins can build on
// its value types with the helpers below.

#ifndef IKCAS_PLUGIN_SHARED
extern const plugin_desc_t ikcas_plugin_arith;
#endif

extern const value_type_t arith_type_int;
extern const value_type_t arith_type_rational;
extern const value_type_t arith_type_float;

/** True if v is an int. */
int arith_is_int(const value_t *v);

/** True if v is a rational. */
int arith_is_rational(const value_t *v);

/** True if v is a float. */
int arith_is_float(const value_t *v);

/** Sign of an int, rational or float value: -1, 0 or 1. */
int arith_sign(const value_t *v);

/** Makes an int value holding a copy of n. */
cas_status_t arith_make_int(cas_t *cas, value_t *out, const bignum_t *n);

/**
 * Makes num/den in lowest terms, as an int if den divides num. Reports a
 * domain error when den is zero.
 */
cas_status_t arith_make_rational(cas_t *cas, value_t *out, const bignum_t *num, const bignum_t *den);

#endif // ARITH_H
