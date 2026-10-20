#ifndef LEXER_H
#define LEXER_H

#include "expr/fwd.h"
#include "input/token.h"

#include <stddef.h>

/**
 * Tokenizer that hands out one token at a time. Number syntax and operator
 * spellings come from the registry, so it doesn't know any plugin by name.
 */
typedef struct {
    cas_t *cas;
    const char *src;
    size_t len;
    size_t pos;
} lexer_t;

/** Starts tokenizing src (len bytes, doesn't need a terminator). */
void lexer_init(lexer_t *lx, cas_t *cas, const char *src, size_t len);

/**
 * Reads the next token. At the end of input it keeps returning TOK_EOF. On
 * a character it doesn't know it reports an error and returns CAS_ERR_SYNTAX.
 */
cas_status_t lexer_next(lexer_t *lx, token_t *out);

#endif // LEXER_H
