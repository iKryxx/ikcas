#include "test_util.h"
#include "arith/arith.h"
#include "engine/plugin.h"

#include <stdlib.h>

/** Collected log output. */
typedef struct {
    char text[4096];
    size_t lines;
    int max_level;
} capture_t;

/** Log sink that appends "level:message" lines to a buffer. */
static void capture_sink(void *user, log_level_t level, const char *msg)
{
    capture_t *c = user;
    size_t n = strlen(c->text);
    snprintf(c->text + n, sizeof(c->text) - n, "%d:%s\n", (int)level, msg);
    c->lines++;
    if ((int)level > c->max_level)
        c->max_level = (int)level;
}

/** Loads arith, evaluates src at the given verbosity, and collects what was logged. */
static void run_with(int verbosity, const char *src, capture_t *c)
{
    memset(c, 0, sizeof(*c));
    cas_options_t opts = { .verbosity = verbosity };
    cas_t *cas = cas_new(&opts);
    cas_set_log_sink(cas, capture_sink, c);
    CHECK(cas_register_plugin(cas, &ikcas_plugin_arith) == CAS_OK);
    free(test_eval(cas, src));
    cas_free(cas);
}

/** Each level logs what it should and nothing from the levels above. */
void run_log_tests(void)
{
    capture_t c;

    run_with(0, "1/2+x*3", &c);
    CHECK(c.lines == 0);

    run_with(LOG_INFO, "1/2+x*3", &c);
    CHECK(c.max_level == LOG_INFO);
    CHECK(strstr(c.text, "1:loaded plugin 'arith' 1.0 (built in)\n") != NULL);
    CHECK(strstr(c.text, "1:eval '1/2+x*3'\n") != NULL);
    CHECK(strstr(c.text, "fold") == NULL);

    run_with(LOG_DEBUG, "1/2+x*3", &c);
    CHECK(c.max_level == LOG_DEBUG);
    CHECK(strstr(c.text, "2:register function 'add' (arith)\n") != NULL);
    CHECK(strstr(c.text, "2:register infix operator '+' -> add (arith)\n") != NULL);
    CHECK(strstr(c.text, "2:pass fold_constants\n") != NULL);
    CHECK(strstr(c.text, "2:fold div(1, 2) -> 1/2\n") != NULL);
    CHECK(strstr(c.text, "token") == NULL);

    run_with(LOG_TRACE, "1+2", &c);
    CHECK(c.max_level == LOG_TRACE);
    CHECK(strstr(c.text, "3:token num '1' @0\n") != NULL);
    CHECK(strstr(c.text, "3:token op '+' @1\n") != NULL);
    CHECK(strstr(c.text, "3:token eof '' @3\n") != NULL);

    // Logging isn't an error, so diagnostics stay empty on success.
    cas_options_t opts = { .verbosity = LOG_TRACE };
    cas_t *cas = cas_new(&opts);
    cas_set_log_sink(cas, capture_sink, &c);
    CHECK(cas_register_plugin(cas, &ikcas_plugin_arith) == CAS_OK);
    CHECK(test_status(cas, "1+2") == CAS_OK);
    CHECK(cas->diags.len == 0);
    cas_free(cas);
}
