//
// Created by ikryxxdev on 2/11/26.
//

#ifndef IKCAS_PARSE_H
#define IKCAS_PARSE_H

#define IDENTIFIER_MAX_LEN 16

#include "core/lex.h"
#include "core/env.h"

typedef struct node node_t;
typedef struct arena arena_t;

typedef struct {
    lex_t lx;
    env_t *env;
    char err[128];
    arena_t *arena;
} parser_t;
typedef enum {
    STMT_EXPR,
    STMT_ASSIGN,
    STMT_FUNCDEF,
} stmt_kind_t;

typedef struct {
    stmt_kind_t kind;
    // NULL if not an assignment; points to parse arena
    const char *name;
    // NULL on error
    node_t *expr;
    const char **params;
    int arity;
} stmt_t;

stmt_t parse_stmnt(arena_t *a, const char *input, const char **err);

#endif //IKCAS_PARSE_H