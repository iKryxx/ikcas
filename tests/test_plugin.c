#include "test_util.h"
#include "arith/arith.h"
#include "engine/plugin.h"

#include <stdlib.h>

/** Evaluates src and compares the printed result with the expected text. */
static void check_eval(cas_t *cas, const char *src, const char *want)
{
    char *got = test_eval(cas, src);
    CHECK_STR(got, want);
    free(got);
}

// A plugin written the way a third party would write one: a postfix
// operator, a constant and a simplification pass.

/** Factorial for small non-negative ints. */
static cas_status_t fact_eval(cas_t *cas, const value_t *args, size_t n, value_t *out)
{
    (void)n;
    if (!arith_is_int(&args[0]) || arith_sign(&args[0]) < 0 || args[0].as.i.len > 1)
        return cas_error(cas, CAS_ERR_DOMAIN, "factorial needs a small non-negative int");

    bignum_t acc, k;
    bignum_init(&acc, allocator_heap());
    bignum_init(&k, allocator_heap());
    bignum_from_i64(&acc, 1);
    for (uint32_t i = 2; i <= args[0].as.i.limb[0]; i++) {
        bignum_from_i64(&k, i);
        bignum_mul(&acc, &acc, &k);
    }
    cas_status_t st = arith_make_int(cas, out, &acc);
    bignum_destroy(&acc);
    bignum_destroy(&k);
    return st;
}

/** The constant 42. */
static cas_status_t answer_make(cas_t *cas, value_t *out)
{
    bignum_t n;
    bignum_init(&n, allocator_heap());
    bignum_from_i64(&n, 42);
    cas_status_t st = arith_make_int(cas, out, &n);
    bignum_destroy(&n);
    return st;
}

/** True if e is the int constant 2. */
static int is_int_two(const expr_t *e)
{
    return expr_is_const(e) && arith_is_int(&e->as.value) && e->as.value.as.i.len == 1 &&
           e->as.value.as.i.sign > 0 && e->as.value.as.i.limb[0] == 2;
}

/** expr_map callback: rewrites x^2 to x*x. */
static expr_t *square_node(cas_t *cas, expr_t *e, void *user)
{
    const fn_def_t *pow = registry_find_fn(cas, "pow", 3);
    const fn_def_t *mul = user;
    if (!expr_is_apply_of(e, pow) || !is_int_two(e->as.apply.args[1]))
        return e;
    expr_t *x = e->as.apply.args[0];
    expr_t *factors[] = { x, x };
    return expr_new_apply(cas, e->span, mul, factors, 2);
}

/** Pass that writes squares as products. arith has no rule for this. */
static expr_t *square_pass(cas_t *cas, expr_t *root)
{
    return expr_map(cas, root, square_node, (void *)registry_find_fn(cas, "mul", 3));
}

/** expr_map callback: renames the symbol "hello" to "HELLO". */
static expr_t *shout_node(cas_t *cas, expr_t *e, void *user)
{
    (void)user;
    if (e->kind == EXPR_SYMBOL && e->as.sym.len == 5 && memcmp(e->as.sym.name, "hello", 5) == 0)
        return expr_new_symbol(cas, e->span, "HELLO", 5);
    return e;
}

/** Display pass that shouts "hello". */
static expr_t *shout_pass(cas_t *cas, expr_t *root)
{
    return expr_map(cas, root, shout_node, NULL);
}

static const fn_def_t fn_fact = { "fact", 1, 1, 0, fact_eval, NULL };
static const op_def_t op_fact = { "!", FIX_POSTFIX, 50, ASSOC_LEFT, &fn_fact };
static const constant_def_t const_answer = { "answer", answer_make };
static const pass_def_t pass_square = { "square", square_pass };
static const pass_def_t pass_shout = { "shout", shout_pass };

/** Registers factorial (as a postfix "!"), the constant "answer", the square pass and the shout display pass. */
static cas_status_t demo_init(cas_t *cas)
{
    cas_status_t st = registry_add_fn(cas, &fn_fact);
    if (st == CAS_OK)
        st = registry_add_op(cas, &op_fact);
    if (st == CAS_OK)
        st = registry_add_constant(cas, &const_answer);
    if (st == CAS_OK)
        st = registry_add_pass(cas, &pass_square);
    if (st == CAS_OK)
        st = registry_add_display_pass(cas, &pass_shout);
    return st;
}

static int demo_shutdowns;

/** Counts shutdown calls so the test can check they happen. */
static void demo_shutdown(cas_t *cas)
{
    (void)cas;
    demo_shutdowns++;
}

IKCAS_PLUGIN(demo, "0.1", demo_init, demo_shutdown);

// Plugins that have to be rejected.

/** Tries to register a function name that is already taken. */
static cas_status_t clash_init(cas_t *cas)
{
    // arith already registered "add"
    static const fn_def_t again = { "add", 2, 2, 0, NULL, NULL };
    return registry_add_fn(cas, &again);
}
IKCAS_PLUGIN(clash, "1", clash_init, NULL);

static const plugin_desc_t wrong_version = { 999, "future", "1", demo_init, NULL };

/** Tries two invalid operators. Succeeds only if both are rejected. */
static cas_status_t bad_op_init(cas_t *cas)
{
    static const op_def_t mixed = { "a+", FIX_INFIX, 10, ASSOC_LEFT, &fn_fact }; // mixes letters and symbols
    static const op_def_t arity = { "$$", FIX_INFIX, 10, ASSOC_LEFT, &fn_fact };  // infix needs a 2-argument function, fact takes 1
    cas_status_t a = registry_add_op(cas, &mixed);
    cas_status_t b = registry_add_op(cas, &arity);
    return (a != CAS_OK && b != CAS_OK) ? CAS_OK : CAS_ERR_INTERNAL;
}
IKCAS_PLUGIN(bad_op, "1", bad_op_init, NULL);

// Plugins that watch the pass pipeline.

static int counter_runs;
static int churn_runs;
static int unstable_notes;

/** Pass that counts how often the pipeline reaches it and changes nothing. */
static expr_t *counter_pass(cas_t *cas, expr_t *root)
{
    (void)cas;
    counter_runs++;
    return root;
}

/** Pass that never settles: it returns a fresh copy every time. */
static expr_t *churn_pass(cas_t *cas, expr_t *root)
{
    churn_runs++;
    return expr_clone(cas, root);
}

static const pass_def_t pass_counter = { "counter", counter_pass };
static const pass_def_t pass_churn = { "churn", churn_pass };

/** Registers the counting pass. */
static cas_status_t counter_init(cas_t *cas)
{
    return registry_add_pass(cas, &pass_counter);
}
IKCAS_PLUGIN(counter, "1", counter_init, NULL);

/** Registers the pass that never settles. */
static cas_status_t churn_init(cas_t *cas)
{
    return registry_add_pass(cas, &pass_churn);
}
IKCAS_PLUGIN(churn, "1", churn_init, NULL);

/** Log sink that counts the "no stable form" notes. */
static void note_sink(void *user, log_level_t level, const char *msg)
{
    (void)user;
    (void)level;
    if (strstr(msg, "no stable form") != NULL)
        unstable_notes++;
}

/** Static registration, rejections, operator validation, loading the shared object. */
void run_plugin_tests(void)
{
    // registered through the API
    {
        cas_t *cas = test_new_cas(0);
        CHECK(cas_register_plugin(cas, &ikcas_plugin_demo) == CAS_OK);
        check_eval(cas, "3!", "6");
        check_eval(cas, "5!", "120");
        check_eval(cas, "2*3!", "12");
        check_eval(cas, "-3!", "-6");
        check_eval(cas, "(1+2)!", "6");
        check_eval(cas, "answer", "42");
        check_eval(cas, "answer*2+1", "85");
        check_eval(cas, "0-1!", "-1");
        check_eval(cas, "(0-1)!", "error: factorial needs a small non-negative int");
        // The pass runs after folding, so it only sees what is still symbolic.
        check_eval(cas, "x^2", "x * x");
        check_eval(cas, "x^3", "x ^ 3");
        check_eval(cas, "(x+1)^2", "(1 + x) * (1 + x)");
        check_eval(cas, "2^2*x", "4 * x"); // folded before the pass sees it
        check_eval(cas, "2*x", "2 * x");

        // display passes only change what is printed, the result stays as the simplifier left it
        check_eval(cas, "hello", "HELLO");
        check_eval(cas, "hello+1", "1 + HELLO");
        char *canonical = test_canonical(cas, "hello");
        CHECK_STR(canonical, "hello");
        free(canonical);

        // rejected registrations leave a clear message
        CHECK(cas_register_plugin(cas, &ikcas_plugin_demo) == CAS_ERR_PLUGIN);
        cas->diags.len = 0;
        CHECK(cas_register_plugin(cas, &ikcas_plugin_clash) == CAS_ERR_REGISTRY);
        CHECK(cas->diags.len > 0);
        CHECK_STR(cas->diags.items[0].msg, "function 'add' already registered by 'arith'");
        cas->diags.len = 0;
        CHECK(cas_register_plugin(cas, &wrong_version) == CAS_ERR_PLUGIN);
        CHECK(cas->diags.len > 0 && strstr(cas->diags.items[0].msg, "API version") != NULL);
        cas->diags.len = 0;
        CHECK(registry_add_display_pass(cas, &pass_shout) == CAS_ERR_REGISTRY); // duplicate name
        CHECK(cas->diags.len > 0 && strstr(cas->diags.items[0].msg, "display pass 'shout' already registered") != NULL);
        cas->diags.len = 0;

        // a failed plugin must not break what is already registered
        check_eval(cas, "1+2", "3");

        int before = demo_shutdowns;
        cas_free(cas);
        CHECK(demo_shutdowns == before + 1);
    }

    // the pipeline repeats until a round changes nothing
    {
        cas_t *cas = test_new_cas(0);
        CHECK(cas_register_plugin(cas, &ikcas_plugin_counter) == CAS_OK);
        counter_runs = 0;
        check_eval(cas, "x", "x");
        CHECK(counter_runs == 1); // nothing to rewrite, so the first round is already stable
        counter_runs = 0;
        check_eval(cas, "x-y", "x - y");
        CHECK(counter_runs == 2); // sub is rewritten in round 0, round 1 changes nothing
        counter_runs = 0;
        check_eval(cas, "2*x*(1/2)*y", "x * y");
        CHECK(counter_runs == 3); // round 0 folds 2 * 1/2 into a 1, round 1 drops it, round 2 changes nothing
        cas_free(cas);
    }

    // a pass that never settles ends at simplify_max_rounds, with a note
    {
        cas_options_t opts = { .verbosity = LOG_INFO, .simplify_max_rounds = 3 };
        cas_t *cas = cas_new(&opts);
        cas_set_log_sink(cas, note_sink, NULL);
        CHECK(cas_register_plugin(cas, &ikcas_plugin_arith) == CAS_OK);
        CHECK(cas_register_plugin(cas, &ikcas_plugin_churn) == CAS_OK);
        churn_runs = 0;
        unstable_notes = 0;
        check_eval(cas, "x-y", "x - y"); // the result is still right
        CHECK(churn_runs == 3);
        CHECK(unstable_notes == 1);
        cas_free(cas);
    }

    // operator validation
    {
        cas_t *cas = test_new_cas(0);
        CHECK(cas_register_plugin(cas, &ikcas_plugin_bad_op) == CAS_OK);
        cas_free(cas);
    }

    // the same plugin code as a shared object
    {
        cas_t *cas = test_new_cas(0);
        check_eval(cas, "abs(-3)", "error: unknown function 'abs'"); // not loaded yet
        CHECK(cas_load_plugin(cas, EXTRA_OPS_SO) == CAS_OK);
        check_eval(cas, "abs(-3)", "3");
        check_eval(cas, "abs(-3)+2^-1", "7/2");
        check_eval(cas, "abs(float(-1/2))", "0.5");

        CHECK(cas_load_plugin(cas, EXTRA_OPS_SO) == CAS_ERR_PLUGIN); // already loaded
        cas->diags.len = 0;
        CHECK(cas_load_plugin(cas, "build/plugins/does_not_exist.so") == CAS_ERR_PLUGIN);
        CHECK(cas->diags.len > 0);
        cas->diags.len = 0;
        CHECK(cas_load_plugin(cas, "Makefile") == CAS_ERR_PLUGIN); // not a shared object
        cas->diags.len = 0;
        cas_free(cas); // dlclose happens after the registry is gone
    }
}
