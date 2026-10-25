#ifndef SIMPLIFY_H
#define SIMPLIFY_H

#include "engine/cas.h"
#include "expr/op.h"

/**
 * The first pass the engine ships. It replaces every application whose
 * arguments are all constants with the result of the function's eval. On an
 * expression without symbols that is plain evaluation. With symbols it folds
 * what it can: 2*3*x becomes 6*x.
 */
extern const pass_def_t pass_fold_constants;

/**
 * The second pass the engine ships. It normalizes application of associative functions.
 * Nested applications of the same function are flattened (mul(mul(a, b), c) becomes mul(a, b, c)).
 * If the function is also commutative, its constant arguments are folded into one and moved
 * to the front, so 2 * 5 * x * 3 becomes 30 * x. Only functions with FN_ASSOCIATIVE and a variadic
 * max_args are touched.
 */
extern const pass_def_t pass_collect_assoc;

/**
 * The rewrite pass the engine ships, between fold_constants and collect_assoc. It doesn't do much on its own: it calls the rewrite hook of every
 * function that has one. The arithmetic plugin uses this to normalize the operator set:
 * sub(a, b) becomes add(a, mul(-1, b)), div(a, b) becomes mul(a, pow(b, -1)) and neg(a) becomes
 * mul(-1, a), so what is left is a tree of add, mul and pow. A constant operand is negated or
 * inverted directly instead. It runs after fold_constants, so the hooks only see applications that
 * still have a symbol in them, and errors like division by zero are reported by the fold.
 * The display passes (cas_present) undo this for printing.
 */
extern const pass_def_t pass_rewrite_expr;

// cas_simplify (engine/cas.h) runs all registered passes in order, and again until a round changes nothing.

#endif // SIMPLIFY_H
