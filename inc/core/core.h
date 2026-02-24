//
// Created by ikryxxdev on 2/10/26.
//

#ifndef IKCAS_CORE_H
#define IKCAS_CORE_H

typedef struct env env_t;
extern bool c_print_ast;
extern int c_precision; // Negative if no set precision

typedef enum {
    CORE_EVALUATION,
    CORE_ASSIGNMENT,
    CORE_ERROR,
    CORE_PLOT,
    CORE_TABLE
} core_kind_t;
typedef enum { EVAL_MODE_EXACT, EVAL_MODE_APPROX } eval_mode_t;

typedef struct {
    bool ok;
    const char text[128];
    core_kind_t kind;
} core_result_t;

void core_init(void);
void core_shutdown(void);

env_t *core_get_g_env(void);

core_result_t core_eval(const char *expr);
void core_set_eval_mode(eval_mode_t mode);
eval_mode_t core_get_eval_mode(void);

void core_set_precision(int precision);
int core_get_precision(void);

#endif // IKCAS_CORE_H
