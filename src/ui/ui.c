//
// Created by ikryxxdev on 1/16/26.
//

#include "ui/ui.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/core.h"
#include "core/env.h"

static bool detect_bitmap(const struct notcurses *nc) {
    return notcurses_canpixel(nc);
}

static void redraw_input(const ui_t *ui, const char *line) {
    ncplane_erase(ui->in);
    ncplane_putstr(ui->in, "> ");
    ncplane_putstr(ui->in, line);

    // Cursor
    const int y = 0, x = 2 + (int)strlen(line);
    ncplane_cursor_move_yx(ui->in, y, x);
}

static void ui__layout_apply(ui_t *ui) {
    notcurses_term_dim_yx(ui->nc, &ui->rows, &ui->cols);

    ui->left_w = ui->cols / 2;
    ui->right_w = ui->cols - ui->left_w;

    // out: left, rows - 2
    ncplane_resize_simple(ui->out, ui->rows - 2, ui->left_w);
    ncplane_move_yx(ui->out, 0, 0);

    // in: full width, 2 rows bottom
    ncplane_resize_simple(ui->in, 2, ui->cols);
    ncplane_move_yx(ui->in, (int32_t)ui->rows - 2, 0);

    // viz: right, rows - 2
    ncplane_resize_simple(ui->viz, ui->rows - 2, ui->right_w);
    ncplane_move_yx(ui->viz, 0, (int32_t)ui->left_w);

    ui_viz_clear(ui);
}

static cmd_status_t ui__builtin_help(cmd_ctx_t *ctx) {
    // :help [name]
    if (ctx->argc >= 2) {
        const char *name = ctx->argv[1];
        const cmd_def_t *def = cmd_find(ctx->ui->cmds, name);
        if (def) {
            ui_print(ctx->ui, ":%s - %s", def->name,
                     def->help ? def->help : "(no help)");
            return CMD_OK;
        } else {
            ui_print(ctx->ui, "No such command: %s", name);
            return CMD_ERROR;
        }
    }

    ui_print(ctx->ui, "Available commands:");
    const cmd_registry_t *r = (cmd_registry_t *)ctx->user;

    bool *printed = (bool *)calloc((size_t)cmd_count(r), sizeof(bool));
    if (!printed)
        return CMD_ERROR;

    for (int i = 0; i < cmd_count(r); ++i) {
        if (printed[i])
            continue;

        const cmd_def_t *def = cmd_at(r, i);
        const char *help_text = def->help ? def->help : "";

        // Find all commands with the same help text
        int alt_count = 0;
        char alt_names[512] = {0};

        for (int j = i + 1; j < cmd_count(r); ++j) {
            const cmd_def_t *other = cmd_at(r, j);
            const char *other_help = other->help ? other->help : "";

            if (strcmp(help_text, other_help) == 0) {
                if (alt_count == 0) {
                    snprintf(alt_names, sizeof(alt_names), "%s", other->name);
                } else {
                    size_t len = strlen(alt_names);
                    snprintf(alt_names + len, sizeof(alt_names) - len, ", %s",
                             other->name);
                }
                alt_count++;
                printed[j] = true;
            }
        }

        ui_print(ctx->ui, "  :%-12s %s", def->name, help_text);
        if (alt_count > 0) {
            ui_print(ctx->ui, "    Alternative commands: %s", alt_names);
        }

        printed[i] = true;
    }

    free(printed);
    return CMD_OK;
}

static cmd_status_t ui__builtin_quit(cmd_ctx_t *ctx) {
    if (ctx->argc != 1)
        return CMD_ERROR;
    ctx->ui->should_close = true;
    return CMD_OK;
}

static cmd_status_t ui__builtin_mode(cmd_ctx_t *ctx) {
    if (ctx->argc == 1) {
        ui_print(ctx->ui, "Mode: %s",
                 core_get_eval_mode() == EVAL_MODE_APPROX ? "approx" : "exact");
        return CMD_OK;
    }

    if (ctx->argc != 2) {
        ui_print(ctx->ui, "Usage: :mode {exact|approx}");
        return CMD_ERROR;
    }

    if (strcmp(ctx->argv[1], "exact") == 0) {
        core_set_eval_mode(EVAL_MODE_EXACT);
        ui_print(ctx->ui, "Mode: exact");
        return CMD_OK;
    }
    if (strcmp(ctx->argv[1], "approx") == 0) {
        core_set_eval_mode(EVAL_MODE_APPROX);
        ui_print(ctx->ui, "Mode: approx");
        return CMD_OK;
    }

    ui_print(ctx->ui, "Usage: :mode {exact|approx}");
    return CMD_ERROR;
}

static cmd_status_t ui__builtin_precision(cmd_ctx_t *ctx) {
    if (ctx->argc == 1) {
        if (core_get_precision() == -1)
            ui_print(ctx->ui, "Precision: %d (10)", core_get_precision());
        else
            ui_print(ctx->ui, "Precision: %d", core_get_precision());
        return CMD_OK;
    }
    if (ctx->argc != 2) {
        ui_print(ctx->ui, "Usage: :precision <number>");
        return CMD_ERROR;
    }
    if (cmd_is_int(ctx->argv[1])) {
        long conv = strtoimax(ctx->argv[1], nullptr, 10);
        if (conv > 16 || conv < -1) {
            ui_print(ctx->ui, "Please enter a number between -1 and 16.");
            return CMD_ERROR;
        }

        core_set_precision((int)conv);
        ui_print(ctx->ui, "Precision: %d", core_get_precision());
        return CMD_OK;
    }
    ui_print(ctx->ui, "argument 1 has to be a numeric value.");
    return CMD_ERROR;
}

static cmd_status_t ui__builtin_undef(cmd_ctx_t *ctx) {
    if (ctx->argc < 2) {
        ui_print(ctx->ui, "Usage: :undef <symbol1> [symbol2...]");
        return CMD_ERROR;
    }
    for (int i = 1; i < ctx->argc; i++) {
        if (!env_remove(core_get_g_env(), ctx->argv[i])) {
            ui_print(ctx->ui, "Symbol '%s' does not exist or cannot be removed",
                     ctx->argv[i]);
        } else {
            ui_print(ctx->ui, "Symbol '%s' removed", ctx->argv[i]);
        }
    }
    return CMD_OK;
}

static void ui__register_builtins(cmd_registry_t *r, struct ui *ui) {
    (void)ui;
    cmd_register(r, "help", ui__builtin_help, r, "List commands or :help <cmd>",
                 0);
    cmd_register(r, "mode", ui__builtin_mode, r,
                 "Set eval mode: :mode {exact|approx}", 0);
    cmd_register(r, "quit", ui__builtin_quit, r, "Quit ikcas", 0);
    cmd_register(r, "q", ui__builtin_quit, r, "Quit ikcas", 0);
    cmd_register(r, "precision", ui__builtin_precision, r,
                 "Set precision for decimal numbers: :precision <number", 0);
    cmd_register(r, "undef", ui__builtin_undef, r,
                 "Undefine a symbol: :undef <symbol1> [symbol2...]", 0);
}

bool ui_init(ui_t *ui) {
    memset(ui, 0, sizeof(*ui));

    struct notcurses_options opts = {};
    // Supress startup Info
    opts.flags |= NCOPTION_SUPPRESS_BANNERS;

    ui->nc = notcurses_init(&opts, stdout);
    if (!ui->nc)
        return false;

    ui->root = notcurses_stdplane(ui->nc);

    struct ncplane_options po = {0};
    po.y = 0;
    po.x = 0;
    po.rows = 1;
    po.cols = 1;
    po.name = "out";
    ui->out = ncplane_create(ui->root, &po);
    if (!ui->out)
        return false;
    ncplane_set_scrolling(ui->out, true);

    po.y = 0;
    po.x = 0;
    po.rows = 1;
    po.cols = 1;
    po.name = "in";
    ui->in = ncplane_create(ui->root, &po);
    if (!ui->in)
        return false;

    po.y = 0;
    po.x = 0;
    po.rows = 1;
    po.cols = 1;
    po.name = "viz";
    ui->viz = ncplane_create(ui->root, &po);
    if (!ui->viz)
        return false;

    ui->has_bitmap = detect_bitmap(ui->nc);
    ui->should_close = false;

    ui__layout_apply(ui);

    ui_print(ui, "ikcas version %s", IKCAS_VERSION);
    ui_print(ui, "type :help for a list of commands");
    ui_refresh(ui);

    ui->cmds = cmd_registry_create();
    ui__register_builtins(ui->cmds, ui);

    return true;
}

void ui_shutdown(ui_t *ui) {
    if (!ui)
        return;
    if (ui->nc)
        notcurses_stop(ui->nc);
    memset(ui, 0, sizeof(*ui));
}

void ui_print(const ui_t *ui, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ncplane_putstr(ui->out, buf);
    ncplane_putstr(ui->out, "\n");
}

bool ui_readline(ui_t *ui, char *buf, int cap) {
    buf[0] = 0;
    int len = 0;

    redraw_input(ui, buf);
    ui_refresh(ui);

    for (;;) {
        struct ncinput ni;
        const unsigned int id = notcurses_get_blocking(ui->nc, &ni);

        if (id == NCKEY_EOF)
            return false;
        if (id == NCKEY_RESIZE) {
            notcurses_term_dim_yx(ui->nc, &ui->rows, &ui->cols);
            redraw_input(ui, buf);
            ui_refresh(ui);
            continue;
        }
        if (id == NCKEY_ENTER) {
            buf[len] = 0;
            ncplane_putstr(ui->in, "\n");
            ui_refresh(ui);
            return true;
        }
        if (id == NCKEY_BACKSPACE && len != 0) {
            buf[--len] = 0;
            redraw_input(ui, buf);
            ui_refresh(ui);
        }

        if (ni.ctrl && (id == 'd' || id == 'c' || id == 'D' || id == 'C'))
            return false;

        // printable
        if (id >= 32 && id < 127) {
            if (len < cap - 1) {
                buf[len++] = (char)id;
                buf[len] = 0;
                redraw_input(ui, buf);
                ui_refresh(ui);
            }
        }
    }
}

void ui_refresh(const ui_t *ui) { notcurses_render(ui->nc); }

void ui_viz_clear(ui_t *ui) {
    if (ui->viz_obj) {
        ncplane_destroy(ui->viz_obj);
        ui->viz_obj = nullptr;
    }
    ncplane_erase(ui->viz);
}

void ui_viz_title(ui_t *ui, const char *title) {
    CTI(ul);
    CTI(ur);
    CTI(ll);
    CTI(lr);
    CTI(hline);
    CTI(vline);

    ncplane_perimeter(ui->viz, &ul, &ur, &ll, &lr, &hline, &vline, 0);
    if (title && *title)
        ncplane_putstr_yx(ui->viz, 0, 2, title);
}
