#include "test_util.h"
#include "input/lexer.h"

#include <stdlib.h>

/** Lexes src into "kind:text" pairs separated by spaces, or "error: msg @column" on failure. */
static char *lex_all(cas_t *cas, const char *src)
{
    static const char *const names[] = { "eof", "num", "ident", "op", "lparen", "rparen", "comma" };
    static_assert(sizeof(names) / sizeof(*names) == TOK_COUNT, "token names out of date");

    char *out = calloc(1, 512);
    size_t n = 0;
    lexer_t lx;
    lexer_init(&lx, cas, src, strlen(src));

    for (;;) {
        token_t t;
        if (lexer_next(&lx, &t) != CAS_OK) {
            snprintf(out, 512, "error: %s @%zu", cas->diags.items[0].msg, cas->diags.items[0].span.begin);
            break;
        }
        if (t.kind == TOK_EOF)
            break;
        n += (size_t)snprintf(out + n, 512 - n, "%s%s:%.*s", n ? " " : "", names[t.kind],
                              (int)t.span.len, src + t.span.begin);
    }
    cas_scratch_reset(cas);
    return out;
}

/** Lexes src and compares with the expected token list. */
static void check_lex(cas_t *cas, const char *src, const char *want)
{
    char *got = lex_all(cas, src);
    CHECK_STR(got, want);
    free(got);
}

// Longest-match tests need two operators that share a prefix. No shipped
// plugin has that, so these are dummies.
static const fn_def_t dummy_fn = { "dummy", 2, 2, 0, NULL, NULL };
static const op_def_t star_star = { "**", FIX_INFIX, 40, ASSOC_RIGHT, &dummy_fn };
static const op_def_t word_op = { "mod", FIX_INFIX, 20, ASSOC_LEFT, &dummy_fn };

/** Whitespace, numbers, identifiers, punctuation, longest-match operators, bad characters. */
void run_lexer_tests(void)
{
    cas_t *cas = test_new_cas(0);

    check_lex(cas, "", "");
    check_lex(cas, "   \t ", "");
    check_lex(cas, "12+3", "num:12 op:+ num:3");
    check_lex(cas, " 1 +  2*(3 - 4) ", "num:1 op:+ num:2 op:* lparen:( num:3 op:- num:4 rparen:)");
    check_lex(cas, "x_1 + foo2", "ident:x_1 op:+ ident:foo2");
    check_lex(cas, "f(a,b)", "ident:f lparen:( ident:a comma:, ident:b rparen:)");
    check_lex(cas, "2x", "num:2 ident:x");
    check_lex(cas, "1 $ 2", "error: unknown token '$' @2");
    check_lex(cas, "12 @", "error: unknown token '@' @3");

    CHECK(registry_add_op(cas, &star_star) == CAS_OK);
    CHECK(registry_add_op(cas, &word_op) == CAS_OK);
    check_lex(cas, "2**3*4", "num:2 op:** num:3 op:* num:4");
    check_lex(cas, "7 mod 3", "num:7 op:mod num:3");
    check_lex(cas, "modulo", "ident:modulo");

    cas_free(cas);
}
