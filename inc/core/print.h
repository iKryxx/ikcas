//
// Created by ikryxxdev on 2/13/26.
//

#ifndef IKCAS_PRINT_H
#define IKCAS_PRINT_H

#include "core/node.h"

typedef struct {
    char *buf;
    int cap;
    int len;
} sb_t;

void sb_init(sb_t *sb, char *buf, int cap);
void sb_puts(sb_t *sb, const char *s);
void sb_putc(sb_t *sb, char c);

void node_print(sb_t *sb, node_t *n, int parent_prec);

#endif // IKCAS_PRINT_H
