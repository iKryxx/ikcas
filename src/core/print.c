//
// Created by ikryxxdev on 2/13/26.
//

#include "core/print.h"

#include <string.h>

void sb_init(sb_t *sb, char *buf, int cap) {
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    if (cap > 0) sb->buf[0] = '\0';
}

void sb_puts(sb_t *sb, const char *s) {
    int n = (int) strlen(s);
    if (sb->len + n >= sb->cap) n = sb->cap - 1 - sb->len;
    if (n <= 0) return;
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_putc(sb_t *sb, char c) {
    if (sb->len + 1 >= sb->cap) return;
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static int prec(const node_t* n) {
    switch (n->kind) {
        case NODE_ADD:
        case NODE_SUB:
            return 10;
        case NODE_MUL:
        case NODE_DIV:
            return 20;
        case NODE_POW:
            return 30;
        case NODE_NEG:
            return 25;
        default: return 100;
    }
}

static void paren_if(sb_t *sb, bool yes) {
    if (yes) sb_putc(sb, '(');
}

static void paren_end(sb_t *sb, bool yes) {
    if (yes) sb_putc(sb, ')');
}


void node_print(sb_t *sb, node_t *n, int parent_prec) {
    int p = prec(n);
    bool need_paren = p < parent_prec;

    paren_if(sb, need_paren);

    char tmp[128];
    switch (n->kind) {
        case NODE_RAT:
            rat_to_str(n->rat, tmp, sizeof(tmp));
            sb_puts(sb, tmp);
            break;
        case NODE_REAL:
            snprintf(tmp, sizeof(tmp), "%.17g", n->real);
            sb_puts(sb, tmp);
            break;
        case NODE_SYMBOL:
            sb_puts(sb, n->symbol);
            break;
        case NODE_NEG:
            sb_putc(sb, '-');
            node_print(sb, n->unary.a, p);
            break;
        case NODE_ADD:
            node_print(sb, n->bin.a, p);
            sb_puts(sb, " + ");
            node_print(sb, n->bin.b, p + 1);
            break;
        case NODE_SUB:
            node_print(sb, n->bin.a, p);
            sb_puts(sb, " - ");
            node_print(sb, n->bin.b, p + 1);
            break;
        case NODE_MUL:
            node_print(sb, n->bin.a, p);
            sb_puts(sb, " * ");
            node_print(sb, n->bin.b, p + 1);
            break;
        case NODE_DIV:
            node_print(sb, n->bin.a, p);
            sb_puts(sb, " / ");
            node_print(sb, n->bin.b, p + 1);
            break;
        case NODE_POW:
            node_print(sb, n->bin.a, p);
            sb_puts(sb, "^");
            node_print(sb, n->bin.b, p - 1);
            break;
        case NODE_CALL:
            sb_puts(sb, n->call.name);
            sb_putc(sb, '(');
            for (int i = 0; i < n->call.argc; i++) {
                if (i) sb_puts(sb, ", ");
                node_print(sb, n->call.args[i], 0);
            }
            sb_putc(sb, ')');
            break;
        case NODE_CALLABLE:
            if (n->callable->kind == CALL_BUILTIN) {
                sb_puts(sb, n->callable->builtin->name);
            } else {
                sb_puts(sb, n->callable->builtin->name);
            }
            sb_puts(sb, "()");
            break;
    }

    paren_end(sb, need_paren);
}