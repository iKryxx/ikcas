#ifndef SIMPLIFY_H
#define SIMPLIFY_H

#include "engine/cas.h"
#include "expr/op.h"

/**
 * The one pass the engine ships. It replaces every application whose
 * arguments are all constants with the result of the function's eval. On an
 * expression without symbols that is plain evaluation. With symbols it folds
 * what it can: 2*3*x becomes 6*x.
 */
extern const pass_def_t pass_fold_constants;

// cas_simplify (engine/cas.h) runs all registered passes in order.

#endif // SIMPLIFY_H
