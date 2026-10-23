#include "test_util.h"
#include "arith/arith.h"
#include "engine/plugin.h"
#include "output/printer.h"

#include <stdlib.h>

// extra_ops is linked into the test binary, so operator tests don't depend on
// the shared object loading.
extern const plugin_desc_t ikcas_plugin_extra_ops;

/** Creates a session with arith (and optionally extra_ops). Aborts if registration fails. */
cas_t *test_new_cas(int with_extra)
{
    cas_t *cas = cas_new(NULL);
    if (!cas || cas_register_plugin(cas, &ikcas_plugin_arith) != CAS_OK)
        abort();
    if (with_extra && cas_register_plugin(cas, &ikcas_plugin_extra_ops) != CAS_OK)
        abort();
    return cas;
}

/** "error: <message>" for the first diagnostic. The caller frees it. */
static char *render_first_diag(cas_t *cas)
{
    char buf[DIAG_MSG_MAX + 16];
    snprintf(buf, sizeof(buf), "error: %s", cas->diags.items[0].msg);
    return strdup(buf);
}

/** Which tree eval_with prints. */
typedef enum {
    SHOW_DISPLAY,   // the result after the display passes
    SHOW_TREE,      // the parse tree
    SHOW_CANONICAL, // the result before the display passes
} show_t;

/** Evaluates src and prints the chosen tree, then resets the scratch arena. */
static char *eval_with(cas_t *cas, const char *src, show_t show)
{
    cas_result_t res;
    char *out;
    expr_t *shown = NULL;
    if (cas_eval(cas, src, strlen(src), &res) != CAS_OK ||
        !(shown = show == SHOW_TREE        ? res.tree
                  : show == SHOW_CANONICAL ? res.result
                                           : cas_present(cas, res.result)))
        out = cas->diags.len ? render_first_diag(cas) : strdup("error: (no diagnostic)");
    else
        out = expr_to_str(cas, shown);
    cas_scratch_reset(cas);
    return out;
}

/** Result text of evaluating src. */
char *test_eval(cas_t *cas, const char *src)
{
    return eval_with(cas, src, SHOW_DISPLAY);
}

/** Text of the unsimplified parse tree. */
char *test_parse(cas_t *cas, const char *src)
{
    return eval_with(cas, src, SHOW_TREE);
}

/** Text of the simplified result as the passes leave it, without the display passes. */
char *test_canonical(cas_t *cas, const char *src)
{
    return eval_with(cas, src, SHOW_CANONICAL);
}

/** Status of evaluating src. */
cas_status_t test_status(cas_t *cas, const char *src)
{
    cas_result_t res;
    cas_status_t st = cas_eval(cas, src, strlen(src), &res);
    cas_scratch_reset(cas);
    return st;
}
