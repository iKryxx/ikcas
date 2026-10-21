#include "test_util.h"
#include "arith/arith.h"
#include "engine/plugin.h"

#include <stdlib.h>

extern const plugin_desc_t ikcas_plugin_constants;

// First 72 decimals of each constant, from the same source as the plugin.
#define PI_72 "3.141592653589793238462643383279502884197169399375105820974944592307816406"
#define E_72 "2.718281828459045235360287471352662497757247093699959574966967627724076630"
#define PHI_72 "1.618033988749894848204586834365638117720309179805762862135448622705260462"

/** New session with arith and constants at the given float precision (0 = default). */
static cas_t *new_cas(size_t float_bits)
{
    cas_options_t opts = { .float_bits = float_bits };
    cas_t *cas = cas_new(&opts);
    if (!cas || cas_register_plugin(cas, &ikcas_plugin_arith) != CAS_OK ||
        cas_register_plugin(cas, &ikcas_plugin_constants) != CAS_OK)
        abort();
    return cas;
}

/** Evaluates src and compares the printed result with the expected text. */
static void check_eval(cas_t *cas, const char *src, const char *want)
{
    char *got = test_eval(cas, src);
    CHECK_STR(got, want);
    free(got);
}

/** Evaluates src and checks that the result starts with prefix. */
static void check_prefix(cas_t *cas, const char *src, const char *prefix)
{
    char *got = test_eval(cas, src);
    CHECK(got && strncmp(got, prefix, strlen(prefix)) == 0);
    if (got && strncmp(got, prefix, strlen(prefix)) != 0)
        printf("  %s gave %s\n", src, got);
    free(got);
}

void run_constants_tests(void)
{
    // default precision (128 bits) prints 38 decimals
    cas_t *cas = new_cas(0);
    check_eval(cas, "pi", "3.14159265358979323846264338327950288419");
    check_eval(cas, "e", "2.71828182845904523536028747135266249775");
    check_eval(cas, "phi", "1.61803398874989484820458683436563811772");

    // constants are floats, so they mix with everything else
    check_eval(cas, "2*pi", "6.28318530717958647692528676655900576839");
    check_eval(cas, "e + 1", "3.71828182845904523536028747135266249775");
    check_eval(cas, "-pi", "-3.14159265358979323846264338327950288419");
    check_eval(cas, "pi - pi", "0");
    check_eval(cas, "pi/pi", "1");
    check_eval(cas, "1/2 + pi", "3.64159265358979323846264338327950288419");

    // still symbolic around a symbol, with the constant folded in
    check_eval(cas, "2*pi*x", "6.28318530717958647692528676655900576839 * x");
    check_eval(cas, "x*pi", "3.14159265358979323846264338327950288419 * x");

    // names that aren't constants stay symbols
    check_eval(cas, "pi2", "pi2");
    check_eval(cas, "p", "p");
    check_eval(cas, "x + e", "2.71828182845904523536028747135266249775 + x");

    // a constant isn't a function
    check_eval(cas, "pi(1)", "error: unknown function 'pi'");
    cas_free(cas);

    // higher precision gives more correct digits
    cas = new_cas(256);
    check_prefix(cas, "pi", PI_72);
    check_prefix(cas, "e", E_72);
    check_prefix(cas, "phi", PHI_72);
    cas_free(cas);

    // asking for more bits than the digits can back up stops at the limit
    cas_t *capped = new_cas(320);
    cas_t *huge = new_cas(100000);
    char *a = test_eval(capped, "pi");
    char *b = test_eval(huge, "pi");
    CHECK_STR(a, b);
    free(a);
    free(b);
    cas_free(capped);
    cas_free(huge);

    // the constants need arith's float type, and can't be registered twice
    cas = cas_new(NULL);
    CHECK(cas_register_plugin(cas, &ikcas_plugin_arith) == CAS_OK);
    CHECK(cas_register_plugin(cas, &ikcas_plugin_constants) == CAS_OK);
    CHECK(cas_register_plugin(cas, &ikcas_plugin_constants) == CAS_ERR_PLUGIN);
    cas_free(cas);

    // the same plugin as a shared object
    cas = cas_new(NULL);
    CHECK(cas_register_plugin(cas, &ikcas_plugin_arith) == CAS_OK);
    CHECK(cas_load_plugin(cas, CONSTANTS_SO) == CAS_OK);
    check_eval(cas, "pi", "3.14159265358979323846264338327950288419");
    check_eval(cas, "phi*2", "3.23606797749978969640917366873127623544");
    cas_free(cas);
}
