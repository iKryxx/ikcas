//
// Created by ikryxxdev on 2/11/26.
//

#include "core/cmd.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct cmd_registry {
    cmd_def_t *v;
    int len, cap;
};

static char* dupstr(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) return nullptr;
    memcpy(p, s, n + 1);
    return p;
}

cmd_registry_t * cmd_registry_create(void) {
    cmd_registry_t* r = malloc(sizeof(*r));
    memset(r, 0, sizeof(*r));
    return r;
}

void cmd_registry_destroy(cmd_registry_t *r) {
    if (!r) return;
    for (int i = 0; i < r->len; ++i) {
        free(r->v[i].name);
        free(r->v[i].help);
    }
    free(r->v);
    free(r);
}

static bool ensure_cap(cmd_registry_t *r, int need) {
    if (need <= r->cap) return true;
    int ncap = r->cap ? r->cap * 2 : 16;
    while (ncap < need) ncap *= 2;
    cmd_def_t *nv = realloc(r->v, ncap * sizeof(cmd_def_t));
    if (!nv) return false;
    r->v = nv; r->cap = ncap;
    return true;
}

static int find_idx(const cmd_registry_t *r, const char *name) {
    for (int i = 0; i < r->len; ++i) {
        if (strcmp(r->v[i].name, name) == 0) return i;
    }
    return -1;
}

bool cmd_register(cmd_registry_t *r, const char *name, cmd_fn_t fn, void *user, const char *help, unsigned flags) {
    if (!r || !name || !*name || !fn) return false;

    if (find_idx(r, name) >= 0) return false; // already registered
    if (!ensure_cap(r, r->len + 1)) return false;

    cmd_def_t *def = &r->v[r->len++];
    def->name = dupstr(name);
    def->help = dupstr(help);
    def->fn = fn;
    def->user = user;
    def->flags = flags;

    if (!def->name || !def->help) return false;
    return true;
}

const cmd_def_t * cmd_find(const cmd_registry_t *r, const char *name) {
    int idx = find_idx(r, name);
    return idx >= 0 ? &r->v[idx] : nullptr;
}

int cmd_count(const cmd_registry_t *r) {
    return r ? r->len : 0;
}

const cmd_def_t * cmd_at(const cmd_registry_t *r, int i) {
    if (!r || i < 0 || i >= r->len) return nullptr;
    return &r->v[i];
}

static void skip_ws(const char **s) {
    while (**s && isspace((unsigned char)**s)) (*s)++;
}

static int tokenize(const char *s, const char **argv, int max_argv, char *tmp, int tmp_len) {
    size_t n = strlen(s);
    if ((int)n >= tmp_len) n = tmp_len - 1;
    memcpy(tmp, s, n);
    tmp[n] = '\0';

    int argc = 0;
    char *p = tmp;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc >= max_argv) break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') { *p++ = '\0'; }
            continue;
        }
        argv[argc++] = p;
        while (*p && !isspace(*p)) p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

bool cmd_try_exec(cmd_registry_t *r, struct ui *ui, const char *input) {
    if (!r || !ui || !input || !*input) return false;
    if (input[0] != ':') return false;

    const char *s = input + 1;
    skip_ws(&s);
    if (*s == '\0') return true;

    const int MAX_ARGV = 10;
    const char* argv[MAX_ARGV];
    char tmp[1024];
    int argc = tokenize(s, argv, MAX_ARGV, tmp, sizeof(tmp));
    if (argc <= 0) return true;

    const char *cmdname = argv[0];
    const cmd_def_t *def = cmd_find(r, cmdname);
    if (!def) return true;

    cmd_ctx_t ctx;
    ctx.ui = ui;
    ctx.user = def->user;
    ctx.line = input;
    ctx.argc = argc;
    ctx.argv = argv;
    return def->fn(&ctx) == CMD_OK;
}