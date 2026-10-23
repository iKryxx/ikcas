#ifndef OP_H
#define OP_H

#include "expr/fwd.h"
#include "expr/value.h"

#include <limits.h>
#include <stddef.h>

// Descriptors that plugins hand to the registry. The registry keeps the
// pointer, so they have to stay alive until cas_free (use static const).

/** Use as max_args for functions that take any number of arguments. */
#define FN_VARIADIC UINT_MAX

/**
 * Algebraic properties of a function. Rewrite passes are meant to use them
 * (flattening, reordering).
 */
enum {
    FN_COMMUTATIVE = 1u << 0,
    FN_ASSOCIATIVE = 1u << 1,
};

/**
 * Folds constant arguments into a value. args are borrowed (don't destroy
 * them), out is uninitialized.
 */
typedef cas_status_t (*fn_eval_t)(cas_t *cas, const value_t *args, size_t nargs, value_t *out);

/**
 * Optional simplification hook, called by the rewrite_expr pass on every application of the
 * function (children first). Returns a replacement, the node itself to keep it, or NULL after
 * reporting an error.
 */
typedef expr_t *(*fn_rewrite_t)(cas_t *cas, expr_t *apply);

/**
 * A callable: operators, named functions, anything a pass builds. eval may be
 * NULL for functions that only exist symbolically.
 */
struct fn_def {
    const char *name;
    unsigned min_args;
    unsigned max_args;
    unsigned attrs;
    fn_eval_t eval;
    fn_rewrite_t rewrite;
};

typedef enum {
    FIX_PREFIX,
    FIX_INFIX,
    FIX_POSTFIX,
} fixity_t;

typedef enum {
    ASSOC_LEFT,
    ASSOC_RIGHT,
} assoc_t;

/**
 * Syntax for a function. Higher precedence binds tighter. A prefix
 * operator's precedence is compared with the infix operators that come after
 * its operand: with prefix '-' above '*' and below '^', -2*3 is (-2)*3 and
 * -2^2 is -(2^2).
 */
struct op_def {
    const char *symbol;
    fixity_t fixity;
    int precedence;
    assoc_t assoc; // infix only
    const fn_def_t *fn;
};

/**
 * Number syntax. match returns how many bytes of s form a literal (0 for
 * none), make builds the value from that text. The longest match wins.
 */
typedef struct {
    const char *name;
    size_t (*match)(const char *s, size_t len);
    cas_status_t (*make)(cas_t *cas, const char *text, size_t len, value_t *out);
} literal_def_t;

/** A named value like pi. */
typedef struct {
    const char *name;
    cas_status_t (*make)(cas_t *cas, value_t *out);
} constant_def_t;

/**
 * One step of the simplification pipeline, or of the display pipeline that
 * runs on a finished result before it is printed. A pass must not modify the
 * tree it gets. It returns a new root, or the same one if nothing changed.
 */
typedef struct {
    const char *name;
    expr_t *(*run)(cas_t *cas, expr_t *root);
} pass_def_t;

#endif // OP_H
