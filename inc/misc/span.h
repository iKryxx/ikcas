#ifndef SPAN_H
#define SPAN_H

#include <stddef.h>

/** A byte range in the input line. Diagnostics use it to point at the problem. */
typedef struct {
    size_t begin;
    size_t len;
} span_t;

/** Builds a span from a start offset and a length. */
static inline span_t span_make(size_t begin, size_t len)
{
    return (span_t){ begin, len };
}

/** Smallest span that covers both a and b. */
static inline span_t span_join(span_t a, span_t b)
{
    size_t begin = a.begin < b.begin ? a.begin : b.begin;
    size_t end_a = a.begin + a.len;
    size_t end_b = b.begin + b.len;
    return (span_t){ begin, (end_a > end_b ? end_a : end_b) - begin };
}

#endif // SPAN_H
