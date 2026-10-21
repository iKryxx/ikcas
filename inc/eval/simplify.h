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

// cas_simplify (engine/cas.h) runs all registered passes in order.

#endif // SIMPLIFY_H
