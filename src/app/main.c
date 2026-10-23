#include "app/lineedit.h"
#include "engine/cas.h"
#include "engine/plugin.h"
#include "output/printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Command line options. */
typedef struct {
    int show_ast;
    int verbosity;        // number of -v given
    int verbose;
    const char *expr;     // -e
    const char *plugins[16];
    size_t nplugins;
} cli_t;
static cli_t cli = { 0 };

/** Prints the command line help. */
static void usage(FILE *out)
{
    fputs("usage: ikcas [-v[v[v]]] [-p plugin.so]... [--ast] [-e expression]\n"
          "  -v        log to stderr; repeat for more detail (-vv registrations/folding, -vvv tokens)\n"
          "  -p FILE   load a plugin shared object (repeatable)\n"
          "  --ast     print the parsed tree before the result\n"
          "  -e EXPR   evaluate EXPR and exit instead of starting the REPL\n",
          out);
}

/** Parses the command line into cli. Returns nonzero on a bad argument. */
static int parse_args(int argc, char **argv, cli_t *cli)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0) {
            cli->show_ast = 1;
        } else if(strcmp(argv[i], "-v") == 0) {
            cli->verbose = 1;
            cli->verbosity++;
        } else if (argv[i][0] == '-' && argv[i][1] == 'v' && strspn(argv[i] + 1, "v") == strlen(argv[i] + 1)) {
            cli->verbosity += (int)strlen(argv[i] + 1);
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            cli->expr = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            if (cli->nplugins == sizeof(cli->plugins) / sizeof(*cli->plugins)) {
                fputs("ikcas: too many -p options\n", stderr);
                return 1;
            }
            cli->plugins[cli->nplugins++] = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            exit(EXIT_SUCCESS);
        } else {
            usage(stderr);
            return 1;
        }
    }
    return 0;
}

/** Prints every pending diagnostic to stderr. */
static void render_diags(const cas_t *cas, const char *src, size_t len)
{
    for (size_t i = 0; i < cas->diags.len; i++)
        diag_render(&cas->diags.items[i], src, len, stderr);
}

/** Evaluates one line and prints the result or the errors. Returns 0 on success. */
static int run_line(cas_t *cas, const cli_t *cli, const char *src, size_t len)
{
    cas_result_t res;
    expr_t *shown = NULL;
    int rc = 0;

    if (cas_eval(cas, src, len, &res) != CAS_OK || !(shown = cas_present(cas, res.result))) {
        render_diags(cas, src, len);
        rc = 1;
    } else {
        if (cli->show_ast)
            expr_print_tree(cas, res.tree, stdout);
        char *text = expr_to_str(cas, shown);
        printf("eval: %s\n", text ? text : "(out of memory)");
        fflush(stdout); // keeps stdout and stderr in order when both are piped
        free(text);
    }
    cas_scratch_reset(cas);
    return rc;
}

/**
 * Reads lines until EOF and evaluates each. Blank lines are skipped. On a
 * terminal the lines come from the line editor and are kept in the history;
 * piped input is read as plain lines.
 */
static int run_repl(cas_t *cas, const cli_t *cli)
{
    int interactive = isatty(STDIN_FILENO);
    int editing = lineedit_available();
    char *line = NULL;
    size_t cap = 0;

    if (editing) {
        cap = cas->opts.history_line_max; // what is typed always fits a history slot
        line = malloc(cap);
        if (!line)
            return EXIT_FAILURE;
    }

    for (;;) {
        ssize_t n;
        if (editing) {
            size_t len = 0;
            int rc = lineedit_read(cas, "> ", line, cap, &len);
            if (rc < 0) {
                editing = 0; // terminal refused raw mode, carry on with plain reads
                continue;
            }
            if (rc == 0)
                break;
            n = (ssize_t)len;
        } else {
            if (interactive) {
                fputs("> ", stdout);
                fflush(stdout);
            }
            n = getline(&line, &cap, stdin);
            if (n == -1)
                break;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            n--;

        size_t start = 0;
        while (start < (size_t)n && (line[start] == ' ' || line[start] == '\t'))
            start++;
        if (start == (size_t)n)
            continue; // blank line

        if (editing)
            cas_history_add(cas, line, (size_t)n);
        run_line(cas, cli, line, (size_t)n);
    }
    if (interactive)
        putchar('\n');

    free(line);
    return ferror(stdin) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/** Sets up a session, loads plugins, then evaluates -e or runs the REPL. */
int main(int argc, char **argv)
{
    if (parse_args(argc, argv, &cli) != 0)
        return EXIT_FAILURE;

    cas_options_t opts = { 0 };
    opts.verbosity = cli.verbosity > LOG_TRACE ? LOG_TRACE : cli.verbosity;
    cas_t *cas = cas_new(&opts);
    if (!cas) {
        fputs("ikcas: cannot create session\n", stderr);
        return EXIT_FAILURE;
    }

    int rc = EXIT_SUCCESS;
    if (cas_load_builtin(cas) != CAS_OK) {
        render_diags(cas, NULL, 0);
        rc = EXIT_FAILURE;
    }
    for (size_t i = 0; rc == EXIT_SUCCESS && i < cli.nplugins; i++) {
        if (cas_load_plugin(cas, cli.plugins[i]) != CAS_OK) {
            render_diags(cas, NULL, 0);
            rc = EXIT_FAILURE;
        }
    }

    if (rc == EXIT_SUCCESS) {
        if (cli.expr)
            rc = run_line(cas, &cli, cli.expr, strlen(cli.expr)) ? EXIT_FAILURE : EXIT_SUCCESS;
        else
            rc = run_repl(cas, &cli);
    }

    cas_free(cas);
    return rc;
}
