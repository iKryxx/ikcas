#include "eval/simplify.h"
#include "engine/cas.h"
#include "engine/diag.h"
#include "expr/expr.h"
#include "expr/op.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Builds a "fold name(args) -> result" line for the debug log. */
static void log_fold(cas_t *cas, const fn_def_t *fn, const value_t *argv, size_t n, const value_t *result)
{
    char line[256];
    size_t len = (size_t)snprintf(line, sizeof(line), "fold %s(", fn->name);
    for (size_t i = 0; i < n && len < sizeof(line); i++) {
        char *s = value_to_str(&argv[i]);
        len += (size_t)snprintf(line + len, sizeof(line) - len, "%s%s", i ? ", " : "", s ? s : "?");
        free(s);
    }
    char *r = value_to_str(result);
    if (len < sizeof(line))
        snprintf(line + len, sizeof(line) - len, ") -> %s", r ? r : "?");
    free(r);
    cas_log_write(cas, LOG_DEBUG, "%s", line);
}

/** Evaluates fn over constant values and wraps the result in a node. NULL on error */
static expr_t *fold_apply(cas_t *cas, const fn_def_t *fn, const value_t *vals, size_t n, span_t span)
{
    value_t out;
    cas->cur_span = span; // Errors point at the folded expression
    if (fn->eval(cas, vals, n, &out) != CAS_OK)
        return NULL;
    if (cas_verbosity(cas) >= LOG_DEBUG)
        log_fold(cas, fn, vals, n, &out);
    return expr_new_const(cas, span, out);
}

/** Folds one application if all its arguments are constants, otherwise leaves it. */
static expr_t *fold_node(cas_t *cas, expr_t *e, void *user)
{
    (void)user;
    if (e->kind != EXPR_APPLY || !e->as.apply.fn->eval)
        return e;

    size_t n = e->as.apply.nargs;
    value_t *argv = cas_scratch_alloc(cas, (n ? n : 1) * sizeof(*argv));
    if (!argv)
        return NULL;
    for (size_t i = 0; i < n; i++) {
        const expr_t *arg = e->as.apply.args[i];
        if (!expr_is_const(arg))
            return e;
        argv[i] = arg->as.value; // borrowed, eval must not destroy these
    }

    return fold_apply(cas, e->as.apply.fn, argv, n, e->span);
}

/** Flattens nested applications of an associative fn and folds the constants of a commutative one */
static expr_t *collect_node(cas_t *cas, expr_t *e, void *user)
{
    (void)user;
    if (e->kind != EXPR_APPLY)
        return e;
    const fn_def_t *fn = e->as.apply.fn;
    if (!(fn->attrs & FN_ASSOCIATIVE) || fn->max_args != FN_VARIADIC)
        return e;
    int comm = (fn->attrs & FN_COMMUTATIVE) != 0;

    size_t cap = 0;
    for (size_t i = 0; i < e->as.apply.nargs; i++) {
        const expr_t *a = e->as.apply.args[i];
        cap += expr_is_apply_of(a, fn) ? a->as.apply.nargs : 1;
    }

    expr_t **cn = cas_scratch_alloc(cas, cap * sizeof(*cn)); // Constant nodes
    expr_t **rest = cas_scratch_alloc(cas, cap * sizeof(*rest)); // Everything else
    value_t *vals = cas_scratch_alloc(cas, cap * sizeof(*vals)); // Borrowed values for eval
    if (!cn || !rest || !vals)
        return NULL;

    // flatten one level and partition, keeping the original order
    size_t nc = 0, nr = 0;
    for (size_t i = 0; i < e->as.apply.nargs; i++) {
        expr_t *a = e->as.apply.args[i];
        int nested = expr_is_apply_of(a, fn);
        for (size_t j = 0; j < (nested ? a->as.apply.nargs : 1); j++) { // Never used ternaries in a for-loop condition lol
            expr_t *x = nested ? a->as.apply.args[j] : a;
            if (comm && expr_is_const(x) && fn->eval) {
                vals[nc] = x->as.value;
                cn[nc++] = x;
            } else {
                rest[nr++] = x;
            }
        }
    }

    // fold constants, a single constant is reused as-is
    expr_t *folded = nc == 1 ? cn[0] : 0;
    if (nc >= 2 && !(folded = fold_apply(cas, fn, vals, nc, e->span))) {
        return NULL; // fold_apply already reported the error
    }

    size_t total = nr + (folded != NULL);
    if (total == 1)
        return folded ? folded : rest[0];

    expr_t **out = cas_scratch_alloc(cas, total * sizeof(*out));
    if (!out)
        return NULL;

    size_t k = 0;
    if (folded)
        out[k++] = folded; // constants go first: 30 * x, 3 + x

    memcpy(out + k, rest, nr * sizeof(*rest));

    // unchanged: keep the original node
    if (total == e->as.apply.nargs && memcmp(out, e->as.apply.args, total * sizeof(*out)) == 0)
        return e;

    return expr_new_apply(cas, e->span, fn, out, total);
}

/** Replaces one application with whatever its function's rewrite hook returns. Functions without a hook are kept. */
static expr_t *rewrite_nodes(cas_t *cas, expr_t *e, void *user)
{
    (void)user;
    if (e->kind != EXPR_APPLY)
        return e;
    const fn_rewrite_t fn = e->as.apply.fn->rewrite;
    if (!fn)
        return e;
    return fn(cas, e);
}

/** Pass entry point: folds the whole tree bottom-up. */
static expr_t *fold_constants(cas_t *cas, expr_t *root)
{
    return expr_map(cas, root, fold_node, NULL);
}

/** Pass entry point: flattens and folds associative / commutative applications bottom-up. */
static expr_t *collect_assoc(cas_t *cas, expr_t *root) {
    return expr_map(cas, root, collect_node, NULL);
}

/** Pass entry point: rewrites expressions with the initial goal to normalize the operator set */
static expr_t *rewrite_expr(cas_t *cas, expr_t *root) {
    return expr_map(cas, root, rewrite_nodes, NULL);
}

const pass_def_t pass_fold_constants = { "fold_constants", fold_constants };
const pass_def_t pass_collect_assoc  = { "collect_assoc",  collect_assoc };
const pass_def_t pass_rewrite_expr   = { "rewrite_expr",   rewrite_expr};

/** Runs every registered pass in order, stopping at the first failure. */
expr_t *cas_simplify(cas_t *cas, expr_t *root)
{
    const reg_vec_t *passes = &cas->reg.passes;
    for (size_t i = 0; i < passes->len && root; i++) {
        const pass_def_t *p = passes->data[i].def;
        CAS_LOG(cas, LOG_DEBUG, "pass %s", p->name);
        root = p->run(cas, root);
    }
    return root;
}

/** Runs every registered display pass in order, stopping at the first failure. */
expr_t *cas_present(cas_t *cas, expr_t *root)
{
    const reg_vec_t *passes = &cas->reg.display_passes;
    for (size_t i = 0; i < passes->len && root; i++) {
        const pass_def_t *p = passes->data[i].def;
        CAS_LOG(cas, LOG_DEBUG, "display pass %s", p->name);
        root = p->run(cas, root);
    }
    return root;
}
