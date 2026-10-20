#include "test_util.h"

int test_failures;
int test_checks;

/** Runs every frontend suite and reports the totals. */
int main(void)
{
    run_lexer_tests();
    run_parser_tests();
    run_eval_tests();
    run_plugin_tests();
    run_log_tests();

    if (test_failures) {
        printf("%d of %d checks failed\n", test_failures, test_checks);
        return 1;
    }
    printf("all %d frontend checks passed\n", test_checks);
    return 0;
}
