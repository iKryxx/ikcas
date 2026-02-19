//
// Created by ikryxxdev on 2/11/26.
//

#include "core/parse.h"

#include <string.h>

#include "core/node.h"

static bool tok_is(parser_t *p, tok_kind_t k) {return lex_peek(&p->lx).kind == k;}
static tok_t tok_take(parser_t *p) { return lex_next(&p->lx); }
static void parser_set_errormsg(parser_t *p, const char *msg) { memccpy(p->err, msg, '\0', sizeof(p->err)); }
static node_t** parse_arg_list(parser_t *p, int* out_argc);
static const char** parse_param_list(parser_t *p, int* out_arity);
static bool looks_like_funcdef(const char* input);

static bool tok_expect(parser_t *p, tok_kind_t k, const char *msg) {
    if (tok_is(p,k)) { tok_take(p); return true;}
    parser_set_errormsg(p, msg);
    return false;
}

static int lbp(tok_kind_t k) {
    switch (k) {
        case TOK_PLUS:
        case TOK_MINUS:
            return 10;
        case TOK_STAR:
        case TOK_SLASH:
            return 20;
        case TOK_CARET:
            return 30;
        default:
            return 0;
    }
}

static node_t* expr(parser_t* p, int minbp);

static node_t* nud(parser_t *p) {
    tok_t t = tok_take(p);

    if (t.kind == TOK_NUM) {
        return node_rat(p->arena, rat_from_i64(t.i64));
    }

    if (t.kind == TOK_IDENT) {
        char* name = arena_strdup(p->arena, t.beg, t.len);
        if (!name) {
            parser_set_errormsg(p, "Out of memory error");
            return nullptr;
        }

        if (tok_is(p, TOK_LPAREN)) {
            tok_take(p); // eat '('
            int argc = 0;
            node_t** args = parse_arg_list(p, &argc);
            if (!args) return nullptr;
            return node_call(p->arena, name, args, argc);
        }
        return node_symbol(p->arena, t.beg, t.len);
    }

    if (t.kind == TOK_MINUS) {
        node_t* r = expr(p, 25);
        if (!r) return nullptr;
        return node_unary(p->arena, NODE_NEG, r);
    }

    if (t.kind == TOK_LPAREN) {
        node_t* r = expr(p, 0);
        if (!r) return nullptr;
        if (!tok_expect(p, TOK_RPAREN, "expected ')'")) return nullptr;
        return r;
    }
    parser_set_errormsg(p, "unexpected token");
    return nullptr;
}

static node_t* led(parser_t *p, node_t* left, tok_t op) {
    int bp = lbp(op.kind);

    if (op.kind == TOK_PLUS) {
        node_t* r = expr(p, bp);
        if (!r) return r;
        return node_bin(p->arena, NODE_ADD, left, r);
    }
    if (op.kind == TOK_MINUS) {
        node_t* r = expr(p, bp);
        if (!r) return r;
        return node_bin(p->arena, NODE_SUB, left, r);
    }
    if (op.kind == TOK_STAR) {
        node_t* r = expr(p, bp);
        if (!r) return r;
        return node_bin(p->arena, NODE_MUL, left, r);
    }
    if (op.kind == TOK_SLASH) {
        node_t* r = expr(p, bp);
        if (!r) return r;
        return node_bin(p->arena, NODE_DIV, left, r);
    }
    if (op.kind == TOK_CARET) {
        // right associative, parse with bp - 1
        node_t* r = expr(p, bp - 1);
        if (!r) return r;

        return node_bin(p->arena, NODE_POW, left, r);
    }

    parser_set_errormsg(p, "invalid operator");
    return nullptr;
}

static node_t* expr(parser_t *p, int minbp) {
    node_t* left = nud(p);
    if (!left) return left;

    for (;;) {
        tok_t t = lex_peek(&p->lx);
        int bp = lbp(t.kind);
        if (bp <= minbp) break;

        tok_t op = tok_take(p);
        left = led(p, left, op);
        if (!left) return left;
    }
    return left;
}

static node_t** parse_arg_list(parser_t *p, int* out_argc) {
    int cap = 4, len = 0;
    node_t** tmp = arena_alloc(p->arena, cap * sizeof(node_t*));
    if (!tmp) {
        parser_set_errormsg(p, "out of memory");
        return nullptr;
    }

    if (tok_is(p, TOK_RPAREN)) {
        tok_take(p);
        *out_argc = 0;
        return tmp;
    }

    for (;;) {
        node_t* e = expr(p, 0);
        if (!e) return nullptr;
        if (len == cap) {
            int ncap = cap * 2;
            node_t** nt = arena_alloc(p->arena, ncap * sizeof(node_t*));
            if (!nt) {
                parser_set_errormsg(p, "out of memory");
                return nullptr;
            }

            for (int i = 0; i < len; i++) {
                nt[i] = tmp[i];
            }
            tmp = nt;
            cap = ncap;
        }
        tmp[len++] = e;

        if (tok_is(p, TOK_COMMA)) {
            tok_take(p);
            continue;
        }
        if (tok_is(p, TOK_RPAREN)) {
            tok_take(p);
            break;
        }
        parser_set_errormsg(p, "expected ',' or ')'");
        return nullptr;
    }

    *out_argc = len;
    return tmp;
}

const char ** parse_param_list(parser_t *p, int *out_arity) {
    int cap = 4, len = 0;
    const char** tmp = arena_alloc(p->arena, cap * sizeof(const char*));
    if (!tmp) {
        parser_set_errormsg(p, "out of memory");
        return nullptr;
    }

    if (tok_is(p, TOK_RPAREN)) {
        tok_take(p);
        *out_arity = 0;
        return tmp;
    }

    for (;;) {
        tok_t t = lex_peek(&p->lx);
        if (t.kind != TOK_IDENT) {
            parser_set_errormsg(p, "expected parameter name");
            return nullptr;
        }
        tok_take(p);

        char *pname = arena_strdup(p->arena, t.beg, t.len);
        if (!pname) {
            parser_set_errormsg(p, "out of memory");
            return nullptr;
        }

        if (len == cap) {
            int ncap = cap * 2;
            const char** nt = arena_alloc(p->arena, ncap * sizeof(const char*));
            if (!nt) {
                parser_set_errormsg(p, "out of memory");
                return nullptr;
            }

            for (int i = 0; i < len; i++) {
                nt[i] = tmp[i];
            }
            tmp = nt;
            cap = ncap;
        }
        tmp[len++] = pname;
        if (tok_is(p, TOK_COMMA)) {
            tok_take(p);
            continue;
        }
        if (tok_is(p, TOK_RPAREN)) {
            tok_take(p);
            break;
        }

        parser_set_errormsg(p, "expected ',' or ')'");
        return nullptr;
    }
    *out_arity = len;
    return tmp;
}

static bool looks_like_funcdef(const char* input) {
    lex_t lx;
    lex_init(&lx, input);

    if (lex_peek(&lx).kind != TOK_IDENT) return false;
    lex_next(&lx);

    if (lex_peek(&lx).kind != TOK_LPAREN) return false;
    lex_next(&lx);

    if (lex_peek(&lx).kind == TOK_RPAREN) {
        lex_next(&lx);
    } else {
        for (;;) {
            if (lex_peek(&lx).kind != TOK_IDENT) return false;
            lex_next(&lx);

            if (lex_peek(&lx).kind == TOK_COMMA) {
                lex_next(&lx);
                continue;
            }
            if (lex_peek(&lx).kind == TOK_RPAREN) {
                lex_next(&lx);
                break;
            }
            return false;
        }
    }

    return lex_peek(&lx).kind == TOK_EQUALS;
}

stmt_t parse_stmnt(arena_t *a, const char *input, const char **err) {
    parser_t p = {0};
    p.arena = a;
    lex_init(&p.lx, input);

    stmt_t out = { 0 };
    out.kind = STMT_EXPR;

    // Look for: IDENT '(' ... ')' '=' ...
    tok_t t0 = lex_peek(&p.lx);
    if (t0.kind == TOK_IDENT) {
        // take ident but keep its string
        tok_t ident = tok_take(&p);
        if (tok_is(&p, TOK_LPAREN) && looks_like_funcdef(input)) {
            tok_take(&p); // '('
            int arity = 0;
            const char** params = parse_param_list(&p, &arity);
            if (!params) {
                if (err) *err = p.err;
                return (stmt_t){ 0 };
            }

            if (tok_is(&p, TOK_EQUALS)) {
                tok_take(&p);
                node_t* body = expr(&p, 0);
                if (!body) {
                    if (err) *err = p.err;
                    return (stmt_t){ 0 };
                }
                if (lex_peek(&p.lx).kind != TOK_EOF) {
                    parser_set_errormsg(&p, "unexpected trailing input.");
                    return (stmt_t){ 0 };
                }

                out.kind = STMT_FUNCDEF;
                out.name = arena_strdup(a, ident.beg, ident.len);
                out.params = params;
                out.arity = arity;
                out.expr = body;
                if (!out.name) {
                    parser_set_errormsg(&p, "Out of memory error");
                    return (stmt_t){ 0 };
                }
                return out;
            }
            lex_init(&p.lx, input);
        } else if (tok_is(&p, TOK_EQUALS)) {
            tok_take(&p);
            node_t* rhs = expr(&p, 0);
            if (!rhs) {
                if (err) *err = p.err;
                return (stmt_t){ 0 };
            }
            if (lex_peek(&p.lx).kind != TOK_EOF) {
                parser_set_errormsg(&p, "unexpected trailing input");
                return (stmt_t){ 0 };
            }

            out.kind = STMT_ASSIGN;
            out.name = arena_strdup(a, ident.beg, ident.len);
            out.expr = rhs;
            out.arity = 0;
            out.params = nullptr;
            if (!out.name) {
                parser_set_errormsg(&p, "Out of memory");
                return (stmt_t){ 0 };
            }
            return out;
        } else {
            // not assignment, restart lexing
            lex_init(&p.lx, input);
        }
    }

    node_t* r = expr(&p, 0);
    if (!r) {
        if (err) *err = p.err;
        return (stmt_t){ .name = nullptr, .expr = nullptr};
    }

    // must end
    if (lex_peek(&p.lx).kind != TOK_EOF) {
        parser_set_errormsg(&p, "unexpected token");
        return (stmt_t){ .name = nullptr, .expr = nullptr};
    }
    out.kind = STMT_EXPR;
    out.expr = r;
    out.params = nullptr;
    out.arity = 0;
    return out;
}
