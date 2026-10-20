#include "output/printer.h"

#include <stdlib.h>
#include <string.h>

/** Growable string buffer. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom;
} strbuf_t;

/** Appends n bytes. On allocation failure it sets oom and ignores further writes. */
static void sb_append(strbuf_t *sb, const char *s, size_t n)
{
    if (sb->oom)
        return;
    if (sb->len + n + 1 > sb->cap) {
        size_t cap = sb->cap ? sb->cap : 64;
        while (cap < sb->len + n + 1)
            cap *= 2;
        char *p = realloc(sb->data, cap);
        if (!p) {
            sb->oom = 1;
            return;
        }
        sb->data = p;
        sb->cap = cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

/** Appends a C string. */
static void sb_puts(strbuf_t *sb, const char *s)
{
    sb_append(sb, s, strlen(s));
}

// Stands in for the precedence of things that never need parentheses.
#define PREC_ATOM 1000

/** The operator that gives this application its syntax, or NULL if it prints as a call. */
static const op_def_t *op_of(const cas_t *cas, const expr_t *e)
{
    if (e->kind != EXPR_APPLY)
        return NULL;
    const fn_def_t *fn = e->as.apply.fn;
    size_t n = e->as.apply.nargs;
    if (n == 2)
        return registry_op_for_fn(cas, fn, FIX_INFIX);
    if (n == 1) {
        const op_def_t *op = registry_op_for_fn(cas, fn, FIX_PREFIX);
        return op ? op : registry_op_for_fn(cas, fn, FIX_POSTFIX);
    }
    return NULL;
}

/** Precedence of the outermost operator of e, or PREC_ATOM if it never needs parentheses. */
static int prec_of(const cas_t *cas, const expr_t *e)
{
    const op_def_t *op = op_of(cas, e);
    return op ? op->precedence : PREC_ATOM;
}

static void print_expr(const cas_t *cas, const expr_t *e, int in_operator, strbuf_t *sb);

/** Prints e, wrapped in parentheses if asked. */
static void print_operand(const cas_t *cas, const expr_t *e, int parens, strbuf_t *sb)
{
    if (parens)
        sb_puts(sb, "(");
    print_expr(cas, e, 1, sb);
    if (parens)
        sb_puts(sb, ")");
}

/** Prints e as infix text. in_operator is set when e is an operand of an operator. */
static void print_expr(const cas_t *cas, const expr_t *e, int in_operator, strbuf_t *sb)
{
    switch (e->kind) {
    case EXPR_CONST: {
        char *s = value_to_str(&e->as.value);
        if (!s) {
            sb->oom = 1;
            return;
        }
        // Negative numbers and fractions are hard to read next to an operator.
        int wrap = in_operator && (s[0] == '-' || strchr(s, '/'));
        if (wrap)
            sb_puts(sb, "(");
        sb_puts(sb, s);
        if (wrap)
            sb_puts(sb, ")");
        free(s);
        return;
    }
    case EXPR_SYMBOL:
        sb_append(sb, e->as.sym.name, e->as.sym.len);
        return;
    case EXPR_APPLY:
        break;
    }

    const op_def_t *op = op_of(cas, e);
    expr_t *const *args = e->as.apply.args;
    if (!op) {
        sb_puts(sb, e->as.apply.fn->name);
        sb_puts(sb, "(");
        for (size_t i = 0; i < e->as.apply.nargs; i++) {
            if (i)
                sb_puts(sb, ", ");
            print_expr(cas, args[i], 0, sb);
        }
        sb_puts(sb, ")");
        return;
    }

    int p = op->precedence;
    switch (op->fixity) {
    case FIX_INFIX: {
        int lp = prec_of(cas, args[0]), rp = prec_of(cas, args[1]);
        print_operand(cas, args[0], lp < p || (lp == p && op->assoc == ASSOC_RIGHT), sb);
        sb_puts(sb, " ");
        sb_puts(sb, op->symbol);
        sb_puts(sb, " ");
        print_operand(cas, args[1], rp < p || (rp == p && op->assoc == ASSOC_LEFT), sb);
        break;
    }
    case FIX_PREFIX:
        sb_puts(sb, op->symbol);
        print_operand(cas, args[0], prec_of(cas, args[0]) < p, sb);
        break;
    case FIX_POSTFIX:
        print_operand(cas, args[0], prec_of(cas, args[0]) < p, sb);
        sb_puts(sb, op->symbol);
        break;
    }
}

/** Infix text for e as a malloc'd string. */
char *expr_to_str(const cas_t *cas, const expr_t *e)
{
    strbuf_t sb = { 0 };
    print_expr(cas, e, 0, &sb);
    if (sb.oom) {
        free(sb.data);
        return NULL;
    }
    if (!sb.data)
        return strdup("");
    return sb.data;
}

/** Prints one node and recurses. prefix is the indentation inherited from the parents. */
static void print_tree_rec(const cas_t *cas, const expr_t *e, strbuf_t *prefix, int is_last, int is_root, FILE *out)
{
    fputs(prefix->data ? prefix->data : "", out);
    if (!is_root)
        fputs(is_last ? "└── " : "├── ", out);

    size_t saved = prefix->len;
    if (!is_root)
        sb_puts(prefix, is_last ? "    " : "│   ");

    switch (e->kind) {
    case EXPR_CONST: {
        char *s = value_to_str(&e->as.value);
        fprintf(out, "%s\n", s ? s : "?");
        free(s);
        break;
    }
    case EXPR_SYMBOL:
        fprintf(out, "%s\n", e->as.sym.name);
        break;
    case EXPR_APPLY: {
        const op_def_t *op = op_of(cas, e);
        fprintf(out, "%s\n", op && op->fixity == FIX_INFIX ? op->symbol : e->as.apply.fn->name);
        for (size_t i = 0; i < e->as.apply.nargs; i++)
            print_tree_rec(cas, e->as.apply.args[i], prefix, i + 1 == e->as.apply.nargs, 0, out);
        break;
    }
    }

    if (prefix->data) {
        prefix->len = saved;
        prefix->data[saved] = '\0';
    }
}

/** Prints e as an indented tree. */
void expr_print_tree(const cas_t *cas, const expr_t *e, FILE *out)
{
    strbuf_t prefix = { 0 };
    print_tree_rec(cas, e, &prefix, 1, 1, out);
    free(prefix.data);
}
