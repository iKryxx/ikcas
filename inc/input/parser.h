#ifndef PARSER_H
#define PARSER_H

#include "expr/expr.h"
#include "expr/fwd.h"

#include <stddef.h>

#define PARSER_MAX_DEPTH 200

/**
 * Parses the whole input as one expression and builds a tree in the scratch
 * arena. Syntax comes from the registry: operators and their binding powers,
 * number literals, functions, constants. Any other identifier becomes a
 * symbol. On error it reports a diagnostic and leaves *out NULL.
 */
cas_status_t parser_parse(cas_t *cas, const char *src, size_t len, expr_t **out);

#endif // PARSER_H
