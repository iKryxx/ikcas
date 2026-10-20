#ifndef VALUE_H
#define VALUE_H

#include "core/bigfloat.h"
#include "core/bignum.h"
#include "core/bigrational.h"
#include "expr/fwd.h"

/** Binary operations a value type can implement. */
typedef enum {
    VOP_ADD,
    VOP_SUB,
    VOP_MUL,
    VOP_DIV,
    VOP_COUNT,
} value_op_t;

/**
 * A runtime value. `type` says how to read the payload. Plugin types keep
 * their own data behind as.ext.
 */
struct value {
    const value_type_t *type;
    union {
        bignum_t i;
        bigrational_t q;
        bigfloat_t f;
        void *ext;
    } as;
};

/**
 * Arithmetic kernels write into an uninitialized dst and choose the output
 * type themselves. That way int / int can give a rational, and
 * rational + rational can drop back to an int.
 */
typedef cas_status_t (*value_binop_t)(cas_t *cas, value_t *dst, const value_t *a, const value_t *b);
typedef cas_status_t (*value_unop_t)(cas_t *cas, value_t *dst, const value_t *a);

/** Everything the engine needs to know about one kind of value. */
struct value_type {
    const char *name;
    int rank; // promotion order, mixed operands go up to the higher rank

    void (*destroy)(value_t *v);
    cas_status_t (*copy)(cas_t *cas, value_t *dst, const value_t *src);
    char *(*to_str)(const value_t *v); // malloc'd
    // Builds a value of this type from a lower-ranked one. NULL if not possible.
    cas_status_t (*promote_from)(cas_t *cas, value_t *dst, const value_t *src);
    // <0, 0, >0 for two values of this type. NULL if the type has no order.
    int (*cmp)(const value_t *a, const value_t *b);

    value_binop_t binop[VOP_COUNT]; // NULL entries aren't supported
    value_unop_t neg;
};

/** Frees whatever the value owns. The value can't be used afterwards. */
void value_destroy(value_t *v);

/** Deep copy of src into an uninitialized dst. */
cas_status_t value_copy(cas_t *cas, value_t *dst, const value_t *src);

/** Converts src to the given type, or copies it if it already has that type. */
cas_status_t value_convert(cas_t *cas, value_t *dst, const value_t *src, const value_type_t *to);

/** Decimal text of v, malloc'd. The caller frees it. */
char *value_to_str(const value_t *v);

/** True if both values have the same type and compare equal. */
int value_equal(const value_t *a, const value_t *b);

/**
 * Computes a op b across the numeric tower. Both operands are promoted to
 * the higher rank first, then that type's kernel runs.
 */
cas_status_t value_arith(cas_t *cas, value_op_t op, value_t *dst, const value_t *a, const value_t *b);

/** Computes -a. Fails if the type has no negation. */
cas_status_t value_negate(cas_t *cas, value_t *dst, const value_t *a);

#endif // VALUE_H
