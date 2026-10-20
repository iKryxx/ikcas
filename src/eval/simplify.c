#include "eval/simplify.h"

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

    cas->cur_span = e->span;
    value_t out;
    if (e->as.apply.fn->eval(cas, argv, n, &out) != CAS_OK)
        return NULL;
    if (cas_verbosity(cas) >= LOG_DEBUG)
        log_fold(cas, e->as.apply.fn, argv, n, &out);
    return expr_new_const(cas, e->span, out);
}

/** Pass entry point: folds the whole tree bottom-up. */
static expr_t *fold_constants(cas_t *cas, expr_t *root)
{
    return expr_map(cas, root, fold_node, NULL);
}

const pass_def_t pass_fold_constants = { "fold_constants", fold_constants };

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
