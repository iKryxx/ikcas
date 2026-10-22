#ifndef LINEEDIT_H
#define LINEEDIT_H

#include "engine/cas.h"

#include <stddef.h>

/** True if stdin and stdout are a terminal that can take cursor escapes. Otherwise the caller should read plain lines. */
int lineedit_available(void);

/**
 * Reads one line from the terminal with editing and history recall. Raw mode
 * is on only while this runs, so the evaluator's output is printed normally.
 *
 * The line (without newline) is left NUL-terminated in buf, at most cap - 1
 * bytes, and its length goes to *len. Ctrl-C gives an empty line.
 * Returns 1 for a line, 0 on end of input (Ctrl-D on an empty line), -1 if the
 * terminal could not be set up, in which case nothing was consumed.
 */
int lineedit_read(const cas_t *cas, const char *prompt, char *buf, size_t cap,
                  size_t *len);

#endif // LINEEDIT_H
