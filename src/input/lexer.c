#include "input/lexer.h"
#include "engine/cas.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

/** True for characters an identifier can start with. */
static int is_ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

/** True for characters an identifier can contain. */
static int is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/** Points the lexer at the start of src. */
void lexer_init(lexer_t *lx, cas_t *cas, const char *src, size_t len)
{
    lx->cas = cas;
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
}

/** Fills out with a token of the given kind and length, then advances past it. */
static void emit(lexer_t *lx, token_t *out, token_kind_t kind, size_t len)
{
    static const char *const names[] = { "eof", "num", "ident", "op", "lparen", "rparen", "comma" };
    static_assert(sizeof(names) / sizeof(*names) == TOK_COUNT, "token names out of date");
    CAS_LOG(lx->cas, LOG_TRACE, "token %s '%.*s' @%zu", names[kind], (int)len, lx->src + lx->pos, lx->pos);
    out->kind = kind;
    out->span = span_make(lx->pos, len);
    out->lit = NULL;
    lx->pos += len;
}

/**
 * Reads the next token. Tries literals first, then identifiers, then
 * punctuation, then registered operators.
 */
cas_status_t lexer_next(lexer_t *lx, token_t *out)
{
    static_assert(TOK_COUNT == 7, "Exhaustive handling of token kinds in lexer_next");

    while (lx->pos < lx->len && isspace((unsigned char)lx->src[lx->pos]))
        lx->pos++;

    if (lx->pos >= lx->len) {
        emit(lx, out, TOK_EOF, 0);
        return CAS_OK;
    }

    const char *s = lx->src + lx->pos;
    size_t left = lx->len - lx->pos;

    // Literals go first so "0x1f" style syntax can win over "0" and "x1f".
    size_t lit_len;
    const literal_def_t *lit = registry_match_literal(lx->cas, s, left, &lit_len);
    if (lit) {
        emit(lx, out, TOK_NUM, lit_len);
        out->lit = lit;
        return CAS_OK;
    }

    if (is_ident_start(*s)) {
        size_t n = 1;
        while (n < left && is_ident_char(s[n]))
            n++;
        // A word like "mod" is an operator if one is registered under that name.
        emit(lx, out, registry_is_op_symbol(lx->cas, s, n) ? TOK_OP : TOK_IDENT, n);
        return CAS_OK;
    }

    switch (*s) {
    case '(': emit(lx, out, TOK_LPAREN, 1); return CAS_OK;
    case ')': emit(lx, out, TOK_RPAREN, 1); return CAS_OK;
    case ',': emit(lx, out, TOK_COMMA, 1); return CAS_OK;
    default: break;
    }

    const op_def_t *op = registry_match_op(lx->cas, s, left);
    if (op) {
        emit(lx, out, TOK_OP, strlen(op->symbol));
        return CAS_OK;
    }

    return cas_error_at(lx->cas, span_make(lx->pos, 1), CAS_ERR_SYNTAX, "unknown token '%c'", *s);
}
