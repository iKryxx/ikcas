#include "test_util.h"

#include <stdlib.h>

/** Evaluates src and compares the printed result with the expected text. */
static void check_eval(cas_t *cas, const char *src, const char *want)
{
    char *got = test_eval(cas, src);
    CHECK_STR(got, want);
    free(got);
}

/** Integers, exact fractions, errors, symbols, the numeric tower, extra_ops. */
void run_eval_tests(void)
{
    cas_t *cas = test_new_cas(1);

    // integer arithmetic
    check_eval(cas, "1+2*3", "7");
    check_eval(cas, "(1+2)*3", "9");
    check_eval(cas, "10-3-2", "5");
    check_eval(cas, "100/5/2", "10");
    check_eval(cas, "2*(3+4)*5", "70");
    check_eval(cas, "-4*2", "-8");
    check_eval(cas, "-(2+3)", "-5");
    check_eval(cas, "--3", "3");
    check_eval(cas, "0-5", "-5");
    check_eval(cas, "12345678901234567890*98765432109876543210", "1219326311370217952237463801111263526900");

    // exact fractions, collapsing back to ints
    check_eval(cas, "7/2", "7/2");
    check_eval(cas, "6/3", "2");
    check_eval(cas, "0/5", "0");
    check_eval(cas, "-7/2", "-7/2");
    check_eval(cas, "1/-2", "-1/2");
    check_eval(cas, "1/2+1/2", "1");
    check_eval(cas, "1/3+1/6", "1/2");
    check_eval(cas, "(1/2)*4", "2");
    check_eval(cas, "(1/2)/(3/4)", "2/3");
    check_eval(cas, "1/2-1/3", "1/6");
    check_eval(cas, "7/2+1", "9/2");

    // errors
    check_eval(cas, "1/0", "error: division by zero");
    check_eval(cas, "1/(2-2)", "error: division by zero");
    check_eval(cas, "(1/2)/0", "error: division by zero");
    CHECK(test_status(cas, "1/0") == CAS_ERR_DOMAIN);

    // symbols survive; the constant parts around them still fold
    check_eval(cas, "x", "x");
    check_eval(cas, "2*3*x", "6 * x");
    check_eval(cas, "x*2*3", "6 * x");
    check_eval(cas, "5*x*2*3", "30 * x");
    check_eval(cas, "x+1+2", "3 + x");
    check_eval(cas, "(1+2)*x", "3 * x");
    check_eval(cas, "x+1", "1 + x"); // a lone constant still moves to the front
    check_eval(cas, "2*x*3*y*5", "30 * x * y");
    check_eval(cas, "(x+1)+(2+y)+3", "6 + x + y"); // nested sums are flattened
    check_eval(cas, "x+x", "x + x");
    check_eval(cas, "1/0*x*2", "error: division by zero");
    check_eval(cas, "x-x", "x - x");
    check_eval(cas, "1/2*x", "(1/2) * x");
    check_eval(cas, "-x", "-x");
    check_eval(cas, "-(1+2)*x", "(-3) * x");

    // the numeric tower: floats absorb ints and rationals
    char *half = test_eval(cas, "float(1/2)");
    CHECK_STR(half, "0.5");
    free(half);
    char *sum = test_eval(cas, "float(1/2) + 1");
    CHECK_STR(sum, "1.5");
    free(sum);
    char *neg = test_eval(cas, "-float(3)");
    CHECK_STR(neg, "-3");
    free(neg);

    // extra_ops
    check_eval(cas, "2^10", "1024");
    check_eval(cas, "2^3^2", "512");
    check_eval(cas, "-2^2", "-4");
    check_eval(cas, "(-2)^2", "4");
    check_eval(cas, "2^-2", "1/4");
    check_eval(cas, "(7/2)^2", "49/4");
    check_eval(cas, "(2/3)^-2", "9/4");
    check_eval(cas, "5^0", "1");
    check_eval(cas, "0^-1", "error: zero to a negative power");
    check_eval(cas, "abs(-3)", "3");
    check_eval(cas, "abs(3-10)", "7");
    check_eval(cas, "abs(-1/2)", "1/2");
    check_eval(cas, "2^x", "2 ^ x");
    check_eval(cas, "2^(1/2)", "error: ^ needs an int or rational base and an int exponent");

    cas_free(cas);
}
