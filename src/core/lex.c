//
// Created by ikryxxdev on 2/10/26.
//

#include "core/lex.h"

#include <ctype.h>
#include <string.h>

static bool is_ident0(int c) { return isalpha(c) || c == '_';}
static bool is_ident(int c) { return isalnum(c) || c == '_';}

static tok_t make_tok(tok_kind_t kind, const char* beg, int len) {
    tok_t t; memset(&t, 0, sizeof(t));
    t.kind = kind; t.beg = beg; t.len = len;
    return t;
}

static tok_t lex_real(lex_t* l) {
    const char* s = l->s;
    while (s[l->i] && isspace((unsigned char)s[l->i])) l->i++;

    int c = (unsigned char)s[l->i];
    if (c == 0) return make_tok(TOK_EOF, s + l->i, 0);

    const char* beg = s + l->i;

    // number: integer only for now
    if (isdigit(c)) {
        int64_t v = 0;
        while (isdigit((unsigned char)s[l->i])) {
            int d = s[l->i] - '0';
            v *= 10;
            v += d;
            l->i++;
        }

        tok_t t; memset(&t, 0, sizeof(t));
        t = make_tok(TOK_NUM, beg, (int)((s+l->i) - beg));
        t.i64 = v;
        return t;
    }

    if (is_ident0(c)) {
        l->i++;
        while (is_ident((unsigned char)s[l->i])) l->i++;
        return make_tok(TOK_IDENT, beg, (int)((s+l->i) - beg));
    }

    l->i++;
    switch (c) {
        case '+': return make_tok(TOK_PLUS, beg, 1); break;
        case '-': return make_tok(TOK_MINUS, beg, 1); break;
        case '*': return make_tok(TOK_STAR, beg, 1); break;
        case '/': return make_tok(TOK_SLASH, beg, 1); break;
        case '^': return make_tok(TOK_CARET, beg, 1); break;
        case '(': return make_tok(TOK_LPAREN, beg, 1); break;
        case ')': return make_tok(TOK_RPAREN, beg, 1); break;
        case '=': return make_tok(TOK_EQUALS, beg, 1); break;
        case ',': return make_tok(TOK_COMMA, beg, 1); break;
        default: return make_tok(TOK_EOF, beg, 0);
    }
}

static bool is_primary_end(tok_kind_t kind) {
    return kind == TOK_NUM || kind == TOK_IDENT || kind == TOK_RPAREN;
}

static bool is_primary_start(tok_kind_t kind) {
    return kind == TOK_NUM || kind == TOK_IDENT || kind == TOK_LPAREN;
}

void lex_init(lex_t *l, const char *input) {
    memset(l, 0, sizeof(*l));
    l->s = input;
    l->i = 0;
    l->prev_real = make_tok(TOK_EOF, l->s, 0);
    l->cur = lex_real(l);
}

tok_t lex_peek(lex_t *l) {
    if (l->has_injected) return l->injected;
    return l->cur;
}

tok_t lex_next(lex_t *l) {
    if (l->has_injected) {
        l->has_injected = false;
        return l->injected;
    }

    tok_t t = l->cur;
    l->prev_real = t;
    l->cur = lex_real(l);

    if (is_primary_end(t.kind) && is_primary_start(l->cur.kind)) {
        // Don't inject '*' before '(' if previous is identifier (potential function call)
        if (!(t.kind == TOK_IDENT && l->cur.kind == TOK_LPAREN)) {
            l->has_injected = true;
            l->injected = make_tok(TOK_STAR, t.beg + t.len, 0);
        }
    }
    return t;
}

