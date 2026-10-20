#ifndef PRINTER_H
#define PRINTER_H

#include "engine/cas.h"

#include <stdio.h>

/**
 * Prints e as infix text with as few parentheses as the registered operator
 * precedences allow. Returns a malloc'd string, NULL if out of memory.
 */
char *expr_to_str(const cas_t *cas, const expr_t *e);

/**
 * Prints e as a tree, one node per line. Infix operators show their symbol,
 * everything else its function name (so unary minus shows as "neg").
 */
void expr_print_tree(const cas_t *cas, const expr_t *e, FILE *out);

#endif // PRINTER_H
