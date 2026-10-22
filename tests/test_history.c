#include "test_util.h"

/** Lines come back newest first, repeats and unusable lines are not stored. */
static void test_add_and_get(void)
{
    cas_t *cas = cas_new(NULL);
    CHECK(cas != NULL);
    CHECK(cas_history_count(cas) == 0);
    CHECK(cas_history_get(cas, 0) == NULL);

    CHECK(cas_history_add(cas, "1+1", 3) == 0);
    CHECK(cas_history_add(cas, "x*2", 3) == 0);
    CHECK(cas_history_count(cas) == 2);
    CHECK_STR(cas_history_get(cas, 0), "x*2");
    CHECK_STR(cas_history_get(cas, 1), "1+1");
    CHECK(cas_history_get(cas, 2) == NULL);

    // Only the first len bytes count, the rest of the buffer is not part of the line.
    CHECK(cas_history_add(cas, "abcdef", 3) == 0);
    CHECK_STR(cas_history_get(cas, 0), "abc");

    CHECK(cas_history_add(cas, "abc", 3) == 0); // same as the newest
    CHECK(cas_history_count(cas) == 3);
    CHECK(cas_history_add(cas, "1+1", 3) == 0); // same as an older one: stored
    CHECK(cas_history_count(cas) == 4);

    CHECK(cas_history_add(cas, "", 0) == -1);
    CHECK(cas_history_count(cas) == 4);
    cas_free(cas);
}

/** A line that does not fit a slot is refused, never cut short. */
static void test_line_limit(void)
{
    cas_options_t opts = { .history_line_max = 8 };
    cas_t *cas = cas_new(&opts);
    CHECK(cas != NULL);

    CHECK(cas_history_add(cas, "1234567", 7) == 0);
    CHECK(cas_history_add(cas, "12345678", 8) == -1); // no room for the terminator
    CHECK(cas_history_count(cas) == 1);
    CHECK_STR(cas_history_get(cas, 0), "1234567");
    cas_free(cas);
}

/** The oldest line drops out when the history is full. */
static void test_wraparound(void)
{
    cas_options_t opts = { .history_entries = 3 };
    cas_t *cas = cas_new(&opts);
    CHECK(cas != NULL);

    const char *lines[] = { "a", "b", "c", "d", "e" };
    for (size_t i = 0; i < 5; i++)
        CHECK(cas_history_add(cas, lines[i], 1) == 0);

    CHECK(cas_history_count(cas) == 3);
    CHECK_STR(cas_history_get(cas, 0), "e");
    CHECK_STR(cas_history_get(cas, 1), "d");
    CHECK_STR(cas_history_get(cas, 2), "c");
    CHECK(cas_history_get(cas, 3) == NULL);
    cas_free(cas);
}

/** Session history tests. */
void run_history_tests(void)
{
    test_add_and_get();
    test_line_limit();
    test_wraparound();
}
