#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "engine/cas.h"

#include <stdio.h>
#include <string.h>

extern int test_failures;
extern int test_checks;

// Counts a check, and a failure if cond is false.
#define CHECK(cond)                                                            \
    do {                                                                       \
        test_checks++;                                                         \
        if (!(cond)) {                                                         \
            test_failures++;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                      \
    } while (0)

// Counts a check and compares two strings. A NULL got fails.
#define CHECK_STR(got, want)                                                   \
    do {                                                                       \
        const char *g_ = (got), *w_ = (want);                                  \
        test_checks++;                                                         \
        if (!g_ || strcmp(g_, w_) != 0) {                                      \
            test_failures++;                                                   \
            printf("FAIL %s:%d: got \"%s\", expected \"%s\"\n", __FILE__,      \
                   __LINE__, g_ ? g_ : "(null)", w_);                          \
        }                                                                      \
    } while (0)

// Where the tests find the plugin shared objects (run them from the project root).
#define EXTRA_OPS_SO "build/plugins/extra_ops.so"
#define CONSTANTS_SO "build/plugins/constants.so"

/** New session with arith loaded, and extra_ops too if with_extra is set. Both are linked in, not dlopened. */
cas_t *test_new_cas(int with_extra);

/**
 * Evaluates src and returns the result as text, or "error: <message>" for the
 * first diagnostic. Frees all scratch memory before returning. The caller frees the string.
 */
char *test_eval(cas_t *cas, const char *src);

/** Like test_eval, but prints the parse tree before simplification. */
char *test_parse(cas_t *cas, const char *src);

/** Like test_eval, but skips the display passes, so it shows the canonical form (x - y as x + (-1) * y). */
char *test_canonical(cas_t *cas, const char *src);

/** Status code of evaluating src. */
cas_status_t test_status(cas_t *cas, const char *src);

/** Tokenizer tests. */
void run_lexer_tests(void);

/** Parser tests. */
void run_parser_tests(void);

/** Evaluation tests. */
void run_eval_tests(void);

/** Plugin loading and registry tests. */
void run_plugin_tests(void);

/** Log level tests. */
void run_log_tests(void);

/** Tests for the constants plugin. */
void run_constants_tests(void);

/** REPL history tests. */
void run_history_tests(void);

#endif // TEST_UTIL_H
