//
// Created by ikryxxdev on 2/11/26.
//

#ifndef IKCAS_CMD_H
#define IKCAS_CMD_H

struct ui;

typedef enum {
    CMD_OK = 0,
    CMD_ERROR = 1
} cmd_status_t;

typedef struct cmd_ctx {
    struct ui* ui;
    void* user;
    const char *line;
    int argc;
    const char **argv;
} cmd_ctx_t;

typedef cmd_status_t (*cmd_fn_t)(cmd_ctx_t*);

typedef struct cmd_def {
    char *name;       // owned by registry
    char *help;       // owned by registry, may be NULL
    cmd_fn_t fn;
    void *user;
    unsigned flags;
} cmd_def_t;

typedef struct cmd_registry cmd_registry_t;

cmd_registry_t* cmd_registry_create(void);
void cmd_registry_destroy(cmd_registry_t* r);

bool cmd_register(cmd_registry_t* r, const char* name, cmd_fn_t fn, void* user, const char *help, unsigned flags);
const cmd_def_t* cmd_find(const cmd_registry_t* r, const char* name);

bool cmd_try_exec(cmd_registry_t* r, struct ui* ui, const char* input);

int cmd_count(const cmd_registry_t* r);
const cmd_def_t* cmd_at(const cmd_registry_t* r, int i);
#endif //IKCAS_CMD_H