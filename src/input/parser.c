#include "input/parser.h"
#include "engine/cas.h"
#include "input/lexer.h"

#include <assert.h>
#include <string.h>

// Pratt parser. Operators, binding powers and what identifiers mean all
// come from the registry, so a new operator never touches this file.

/** Parser state: the lexer plus one token of lookahead. */
typedef struct {
    cas_t *cas;
    lexer_t lx;
    token_t tok;
    int depth; // current nesting, capped by PARSER_MAX_DEPTH
} parser_t;

static cas_status_t parse_expr(parser_t *p, int min_bp, expr_t **out);

/** Moves to the next token. */
static cas_status_t advance(parser_t *p)
{
    return lexer_next(&p->lx, &p->tok);
}

/** Start of the current token in the input. */
static const char *tok_text(const parser_t *p)
{
    return p->lx.src + p->tok.span.begin;
}

/** Reports a syntax error at the current token. */
static cas_status_t syntax_error(parser_t *p, const char *what)
{
    return cas_error_at(p->cas, p->tok.span, CAS_ERR_SYNTAX, "%s", what);
}

/** Reports the current token as out of place, worded by kind. */
static cas_status_t unexpected(parser_t *p)
{
    static_assert(TOK_COUNT == 7, "Exhaustive handling of token kinds in unexpected");
    switch (p->tok.kind) {
    case TOK_EOF:
        return syntax_error(p, "unexpected end of input");
    case TOK_RPAREN:
        return syntax_error(p, "unmatched ')'");
    case TOK_NUM:
    case TOK_IDENT:
    case TOK_OP:
    case TOK_LPAREN:
    case TOK_COMMA:
        return cas_error_at(p->cas, p->tok.span, CAS_ERR_SYNTAX, "unexpected '%.*s'",
                            (int)p->tok.span.len, tok_text(p));
    case TOK_COUNT:
        break;
    }
    return CAS_ERR_INTERNAL;
}

/** Builds an application node. Maps allocation failure to a status. */
static cas_status_t make_apply(parser_t *p, span_t span, const fn_def_t *fn, expr_t **args, size_t nargs, expr_t **out)
{
    *out = expr_new_apply(p->cas, span, fn, args, nargs);
    return *out ? CAS_OK : CAS_ERR_NOMEM;
}

/** Parses a number literal through the literal that matched it. */
static cas_status_t parse_number(parser_t *p, expr_t **out)
{
    value_t v;
    p->cas->cur_span = p->tok.span;
    cas_status_t st = p->tok.lit->make(p->cas, tok_text(p), p->tok.span.len, &v);
    if (st != CAS_OK)
        return st;
    *out = expr_new_const(p->cas, p->tok.span, v);
    if (!*out)
        return CAS_ERR_NOMEM;
    return advance(p);
}

/** Parses f(a, b, ...) after the name token. Checks that the function exists and the argument count fits. */
static cas_status_t parse_call(parser_t *p, token_t name, expr_t **out)
{
    const char *text = p->lx.src + name.span.begin;
    const fn_def_t *fn = registry_find_fn(p->cas, text, name.span.len);
    if (!fn)
        return cas_error_at(p->cas, name.span, CAS_ERR_SYNTAX, "unknown function '%.*s'", (int)name.span.len, text);

    cas_status_t st = advance(p); // skip the '('
    if (st != CAS_OK)
        return st;

    size_t cap = 4, n = 0;
    expr_t **args = cas_scratch_alloc(p->cas, cap * sizeof(*args));
    if (!args)
        return CAS_ERR_NOMEM;

    if (p->tok.kind != TOK_RPAREN) {
        for (;;) {
            if (n == cap) {
                expr_t **bigger = cas_scratch_alloc(p->cas, cap * 2 * sizeof(*args));
                if (!bigger)
                    return CAS_ERR_NOMEM;
                memcpy(bigger, args, n * sizeof(*args));
                args = bigger;
                cap *= 2;
            }
            if ((st = parse_expr(p, 0, &args[n++])) != CAS_OK)
                return st;
            if (p->tok.kind == TOK_COMMA) {
                if ((st = advance(p)) != CAS_OK)
                    return st;
                continue;
            }
            break;
        }
    }
    if (p->tok.kind != TOK_RPAREN)
        return syntax_error(p, "expected ',' or ')'");

    span_t span = span_join(name.span, p->tok.span);
    if (n < fn->min_args || n > fn->max_args) {
        if (fn->min_args == fn->max_args)
            return cas_error_at(p->cas, span, CAS_ERR_ARITY, "%s expects %u argument(s), got %zu",
                                fn->name, fn->min_args, n);
        return cas_error_at(p->cas, span, CAS_ERR_ARITY, "%s expects at least %u argument(s), got %zu",
                            fn->name, fn->min_args, n);
    }
    if ((st = advance(p)) != CAS_OK) // skip the ')'
        return st;
    return make_apply(p, span, fn, args, n, out);
}

/** Parses an identifier: a call, a registered constant, or a symbol. */
static cas_status_t parse_ident(parser_t *p, expr_t **out)
{
    token_t name = p->tok;
    const char *text = tok_text(p);
    cas_status_t st = advance(p);
    if (st != CAS_OK)
        return st;

    if (p->tok.kind == TOK_LPAREN)
        return parse_call(p, name, out);

    const constant_def_t *c = registry_find_constant(p->cas, text, name.span.len);
    if (c) {
        value_t v;
        p->cas->cur_span = name.span;
        if ((st = c->make(p->cas, &v)) != CAS_OK)
            return st;
        *out = expr_new_const(p->cas, name.span, v);
    } else {
        *out = expr_new_symbol(p->cas, name.span, text, name.span.len);
    }
    return *out ? CAS_OK : CAS_ERR_NOMEM;
}

/** Parses the start of an expression: a primary or a prefix operator. */
static cas_status_t parse_prefix(parser_t *p, expr_t **out)
{
    static_assert(TOK_COUNT == 7, "Exhaustive handling of token kinds in parse_prefix");
    cas_status_t st;

    switch (p->tok.kind) {
    case TOK_NUM:
        return parse_number(p, out);
    case TOK_IDENT:
        return parse_ident(p, out);
    case TOK_OP: {
        token_t optok = p->tok;
        const op_def_t *op = registry_find_op(p->cas, tok_text(p), optok.span.len, FIX_PREFIX);
        if (!op)
            return cas_error_at(p->cas, optok.span, CAS_ERR_SYNTAX, "'%.*s' cannot start an expression",
                                (int)optok.span.len, tok_text(p));
        if ((st = advance(p)) != CAS_OK)
            return st;
        expr_t *operand;
        if ((st = parse_expr(p, op_rbp(op), &operand)) != CAS_OK)
            return st;
        return make_apply(p, span_join(optok.span, operand->span), op->fn, &operand, 1, out);
    }
    case TOK_LPAREN:
        if ((st = advance(p)) != CAS_OK || (st = parse_expr(p, 0, out)) != CAS_OK)
            return st;
        if (p->tok.kind != TOK_RPAREN)
            return syntax_error(p, "expected ')'");
        return advance(p);
    case TOK_EOF:
    case TOK_RPAREN:
    case TOK_COMMA:
        return unexpected(p);
    case TOK_COUNT:
        break;
    }
    return CAS_ERR_INTERNAL;
}

/**
 * Pratt loop. Parses an operand, then keeps folding in infix and postfix
 * operators whose left binding power is at least min_bp.
 */
static cas_status_t parse_expr(parser_t *p, int min_bp, expr_t **out)
{
    if (p->depth >= PARSER_MAX_DEPTH)
        return syntax_error(p, "expression is nested too deeply");
    p->depth++;

    cas_status_t st = parse_prefix(p, out);
    while (st == CAS_OK && p->tok.kind == TOK_OP) {
        token_t optok = p->tok;
        const char *text = tok_text(p);

        const op_def_t *op = registry_find_op(p->cas, text, optok.span.len, FIX_INFIX);
        if (op) {
            // Binds too weakly for this context, so the caller takes it.
            if (op_lbp(op) < min_bp)
                break;
            expr_t *args[2] = { *out };
            if ((st = advance(p)) != CAS_OK || (st = parse_expr(p, op_rbp(op), &args[1])) != CAS_OK)
                break;
            st = make_apply(p, span_join(args[0]->span, args[1]->span), op->fn, args, 2, out);
            continue;
        }

        op = registry_find_op(p->cas, text, optok.span.len, FIX_POSTFIX);
        if (!op || op_lbp(op) < min_bp)
            break;
        expr_t *arg = *out;
        if ((st = advance(p)) != CAS_OK)
            break;
        st = make_apply(p, span_join(arg->span, optok.span), op->fn, &arg, 1, out);
    }

    p->depth--;
    return st;
}

/** Parses the whole input and requires that nothing follows the expression. */
cas_status_t parser_parse(cas_t *cas, const char *src, size_t len, expr_t **out)
{
    parser_t p = { .cas = cas };
    lexer_init(&p.lx, cas, src, len);
    *out = NULL;

    expr_t *root = NULL;
    cas_status_t st = advance(&p);
    if (st == CAS_OK)
        st = parse_expr(&p, 0, &root);
    if (st == CAS_OK && p.tok.kind != TOK_EOF)
        st = unexpected(&p);
    if (st == CAS_OK)
        *out = root;
    return st;
}
