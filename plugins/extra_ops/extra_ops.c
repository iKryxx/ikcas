#include "arith/arith.h"
#include "engine/cas.h"

// Example plugin on top of arith: abs(). List "extra_ops" in plugins.conf to
// build it in, or run `make plugin-extra_ops` and load the .so with `ikcas -p`.

/** Absolute value of any arith number. */
static cas_status_t fn_abs_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    if (arith_sign(&args[0]) < 0)
        return value_negate(cas, out, &args[0]);
    return value_copy(cas, out, &args[0]);
}

static const fn_def_t fn_abs = { "abs", 1, 1, 0, fn_abs_eval, NULL };

/** Registers abs. */
static cas_status_t extra_ops_init(cas_t *cas)
{
    return registry_add_fn(cas, &fn_abs);
}

IKCAS_PLUGIN(extra_ops, "1.0", extra_ops_init, NULL);
