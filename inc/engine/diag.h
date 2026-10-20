#ifndef DIAG_H
#define DIAG_H

#include "expr/fwd.h"
#include "misc/span.h"

#include <stddef.h>
#include <stdio.h>

#define DIAG_MSG_MAX 160
#define DIAG_MAX 16

typedef struct {
    cas_status_t status;
    span_t span;
    char msg[DIAG_MSG_MAX];
} diag_t;

typedef struct {
    diag_t items[DIAG_MAX];
    size_t len;
} diag_list_t;

/**
 * Records an error against cas->cur_span, which the lexer, parser and fold
 * pass keep pointed at what they're working on. Returns status, so callers
 * can write `return cas_error(...)`.
 */
cas_status_t cas_error(cas_t *cas, cas_status_t status, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/** Like cas_error, but with an explicit span. */
cas_status_t cas_error_at(cas_t *cas, span_t span, cas_status_t status, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

// Logging is separate from errors: errors are kept in cas->diags for the
// caller, log lines go to the log sink (stderr by default) if the session's
// verbosity is high enough.

/** Log levels. A session logs everything up to its verbosity. */
typedef enum {
    LOG_INFO = 1,  // -v:   plugins loaded, lines evaluated
    LOG_DEBUG = 2, // -vv:  registrations, passes, constant folding
    LOG_TRACE = 3, // -vvv: every token
} log_level_t;

/** Receives one finished log line (no trailing newline). */
typedef void (*cas_log_sink_t)(void *user, log_level_t level, const char *msg);

/** Redirects log output. Pass fn = NULL to go back to stderr. */
void cas_set_log_sink(cas_t *cas, cas_log_sink_t fn, void *user);

/** Formats and sends a log line unconditionally. Use CAS_LOG to respect the level. */
void cas_log_write(cas_t *cas, log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/** Logs a printf-style message. The arguments aren't evaluated when the level is off. */
#define CAS_LOG(cas, level, ...)                                               \
    do {                                                                       \
        if (cas_verbosity(cas) >= (int)(level))                                \
            cas_log_write((cas), (level), __VA_ARGS__);                        \
    } while (0)

/** Highest log level the session emits (0 = quiet). */
int cas_verbosity(const cas_t *cas);

/** Prints d to out as an error line, followed by the source with a caret under the span. */
void diag_render(const diag_t *d, const char *src, size_t src_len, FILE *out);

#endif // DIAG_H
