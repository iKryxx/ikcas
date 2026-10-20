#ifndef EXPR_FWD_H
#define EXPR_FWD_H

// Forward declarations and the status enum every layer shares. Headers use
// these instead of including each other.

typedef struct cas cas_t;
typedef struct value value_t;
typedef struct value_type value_type_t;
typedef struct expr expr_t;
typedef struct fn_def fn_def_t;
typedef struct op_def op_def_t;

/** Result code returned by most functions that can fail. */
typedef enum {
    CAS_OK = 0,
    CAS_ERR_SYNTAX,
    CAS_ERR_DOMAIN,   // undefined in math, e.g. division by zero
    CAS_ERR_TYPE,     // operation doesn't exist for these operand types
    CAS_ERR_ARITY,    // wrong number of arguments
    CAS_ERR_REGISTRY, // duplicate or invalid registration
    CAS_ERR_PLUGIN,   // plugin failed to load or init
    CAS_ERR_NOMEM,
    CAS_ERR_INTERNAL, // a bug, not bad input
} cas_status_t;

#endif // EXPR_FWD_H
