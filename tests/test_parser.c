#include "test_util.h"

#include <stdlib.h>
#include <string.h>

/**
 * Parses src and compares the tree, printed back with the fewest parentheses
 * that keep its structure, with the expected text. So the printed form pins
 * down the tree.
 */
static void check_parse(cas_t *cas, const char *src, const char *want)
{
    char *got = test_parse(cas, src);
    CHECK_STR(got, want);
    free(got);
}

/** Precedence, associativity, prefix operators, calls, symbols, errors, nesting limits. */
void run_parser_tests(void)
{
    cas_t *cas = test_new_cas(1);

    // precedence and associativity
    check_parse(cas, "1+2*3", "1 + 2 * 3");
    check_parse(cas, "(1+2)*3", "(1 + 2) * 3");
    check_parse(cas, "1-2-3", "1 - 2 - 3");
    check_parse(cas, "1-(2-3)", "1 - (2 - 3)");
    check_parse(cas, "8/4/2", "8 / 4 / 2");
    check_parse(cas, "8/(4/2)", "8 / (4 / 2)");
    check_parse(cas, "2^3^2", "2 ^ 3 ^ 2");
    check_parse(cas, "(2^3)^2", "(2 ^ 3) ^ 2");
    check_parse(cas, "((1))", "1");

    // prefix minus binds tighter than * and looser than ^
    check_parse(cas, "-4*2", "-4 * 2");
    check_parse(cas, "-(4*2)", "-(4 * 2)");
    check_parse(cas, "-(2+3)", "-(2 + 3)");
    check_parse(cas, "--3", "--3");
    check_parse(cas, "2*-3", "2 * -3");
    check_parse(cas, "-2^2", "-2 ^ 2");
    check_parse(cas, "(-2)^2", "(-2) ^ 2");
    check_parse(cas, "2^-1", "2 ^ (-1)");

    // symbols and calls
    check_parse(cas, "x+1", "x + 1");
    check_parse(cas, "abs(x-1)", "abs(x - 1)");
    check_parse(cas, "abs(-3)*2", "abs(-3) * 2");
    check_parse(cas, "  1  +  2  ", "1 + 2");

    // errors
    check_parse(cas, "", "error: unexpected end of input");
    check_parse(cas, "(1+", "error: unexpected end of input");
    check_parse(cas, "(1+2", "error: expected ')'");
    check_parse(cas, "1+2)", "error: unmatched ')'");
    check_parse(cas, "()", "error: unmatched ')'");
    check_parse(cas, "1 2", "error: unexpected '2'");
    check_parse(cas, "2x", "error: unexpected 'x'");
    check_parse(cas, "*3", "error: '*' cannot start an expression");
    check_parse(cas, "1+*3", "error: '*' cannot start an expression");
    check_parse(cas, "1, 2", "error: unexpected ','");
    check_parse(cas, "1 $ 2", "error: unknown token '$'");
    check_parse(cas, "nope(1)", "error: unknown function 'nope'");
    check_parse(cas, "abs()", "error: abs expects 1 argument(s), got 0");
    check_parse(cas, "abs(1,2)", "error: abs expects 1 argument(s), got 2");
    check_parse(cas, "abs(1", "error: expected ',' or ')'");
    CHECK(test_status(cas, "abs(1,2)") == CAS_ERR_ARITY);
    CHECK(test_status(cas, "1+") == CAS_ERR_SYNTAX);

    // deep nesting is an error, not a stack overflow
    char *deep = malloc(2 * 5000 + 2);
    memset(deep, '(', 5000);
    deep[5000] = '1';
    memset(deep + 5001, ')', 5000);
    deep[10001] = '\0';
    check_parse(cas, deep, "error: expression is nested too deeply");
    free(deep);

    // a long flat chain is fine
    char *chain = malloc(4 * 2000 + 2);
    size_t n = 0;
    for (int i = 0; i < 2000; i++)
        n += (size_t)sprintf(chain + n, "1+");
    strcpy(chain + n, "1");
    CHECK(test_status(cas, chain) == CAS_OK);
    free(chain);

    cas_free(cas);
}
