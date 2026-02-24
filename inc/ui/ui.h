//
// Created by ikryxxdev on 1/16/26.
//

#ifndef IKCAS_UI_H
#define IKCAS_UI_H

#include <notcurses/notcurses.h>
#include <stdbool.h>

#include "core/cmd.h"

#define CTI(cname) nccell cname = NCCELL_TRIVIAL_INITIALIZER

typedef struct ui {
    struct notcurses *nc;
    struct ncplane *root;
    struct ncplane *in;
    struct ncplane *out;
    struct ncplane *viz;
    struct ncplane *viz_obj;

    unsigned int rows, cols;
    unsigned int left_w, right_w;

    bool has_bitmap;
    bool should_close;
    cmd_registry_t *cmds;
} ui_t;

extern bool ui_init(ui_t *ui);
extern void ui_shutdown(ui_t *ui);

extern void ui_print(const ui_t *ui, const char *fmt, ...);
extern bool ui_readline(ui_t *ui, char *buf, int cap);
void ui_refresh(const ui_t *ui);

void ui_viz_clear(ui_t *ui);
void ui_viz_title(ui_t *ui, const char *title);

#endif // IKCAS_UI_H
