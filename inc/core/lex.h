//
// Created by ikryxxdev on 2/10/26.
//

#ifndef IKCAS_LEX_H
#define IKCAS_LEX_H
#include <stdint.h>

typedef enum {
    TOK_EOF = 0,
    TOK_NUM,
    TOK_IDENT,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_CARET,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_EQUALS,
    TOK_COMMA
} tok_kind_t;


typedef struct {
    tok_kind_t kind;
    const char *beg;
    int len;

    int64_t i64;
    bool is_decimal;
} tok_t;

typedef struct {
    const char *s;
    int i;
    tok_t cur;

    bool has_injected;
    tok_t injected;
    tok_t prev_real; // last real token
} lex_t;

void lex_init(lex_t *l, const char* input);
tok_t lex_peek(lex_t *l);
tok_t lex_next(lex_t *l);

#endif //IKCAS_LEX_H
