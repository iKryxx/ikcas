#ifndef TOKEN_H
#define TOKEN_H

#include "expr/op.h"
#include "misc/span.h"

/** Kinds of token. */
typedef enum {
    TOK_EOF = 0,
    TOK_NUM,
    TOK_IDENT,
    TOK_OP,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_COUNT,
} token_kind_t;

/** One token: its kind, where it sits in the input, and for numbers the literal that matched. */
typedef struct {
    token_kind_t kind;
    span_t span;
    const literal_def_t *lit; // TOK_NUM only
} token_t;

#endif // TOKEN_H
