#include "engine/diag.h"
#include "engine/cas.h"

#include <stdarg.h>
#include <string.h>

/** Appends a formatted diagnostic. Drops it silently if the list is full. */
static cas_status_t report(cas_t *cas, span_t span, cas_status_t status, const char *fmt, va_list ap)
{
    if (cas->diags.len < DIAG_MAX) {
        diag_t *d = &cas->diags.items[cas->diags.len++];
        d->status = status;
        d->span = span;
        vsnprintf(d->msg, sizeof(d->msg), fmt, ap);
    }
    return status;
}

/** Records an error at the current span. */
cas_status_t cas_error(cas_t *cas, cas_status_t status, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(cas, cas->cur_span, status, fmt, ap);
    va_end(ap);
    return status;
}

/** Records an error at the given span. */
cas_status_t cas_error_at(cas_t *cas, span_t span, cas_status_t status, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(cas, span, status, fmt, ap);
    va_end(ap);
    return status;
}

/** Returns the session's log level. */
int cas_verbosity(const cas_t *cas)
{
    return cas->opts.verbosity;
}

/** Sets or clears the log sink. */
void cas_set_log_sink(cas_t *cas, cas_log_sink_t fn, void *user)
{
    cas->log_sink = fn;
    cas->log_user = user;
}

/** Default log sink: "[level] message" on stderr. */
static void stderr_sink(void *user, log_level_t level, const char *msg)
{
    static const char *const tags[] = { "", "info", "debug", "trace" };
    (void)user;
    fprintf(stderr, "[%s] %s\n", tags[level], msg);
}

/** Formats a log line and hands it to the sink. */
void cas_log_write(cas_t *cas, log_level_t level, const char *fmt, ...)
{
    char msg[DIAG_MSG_MAX * 2];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (cas->log_sink)
        cas->log_sink(cas->log_user, level, msg);
    else
        stderr_sink(NULL, level, msg);
}

/** Prints the error, the input line, and carets under the offending span. */
void diag_render(const diag_t *d, const char *src, size_t src_len, FILE *out)
{
    fprintf(out, "error: %s\n", d->msg);
    if (!src || d->span.begin > src_len)
        return;

    fprintf(out, "  %.*s\n  ", (int)src_len, src);
    for (size_t i = 0; i < d->span.begin; i++)
        fputc(' ', out);
    size_t marks = d->span.len ? d->span.len : 1;
    for (size_t i = 0; i < marks; i++)
        fputc('^', out);
    fputc('\n', out);
}
