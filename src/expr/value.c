#include "expr/value.h"
#include "engine/cas.h"

/** Frees the value's storage. */
void value_destroy(value_t *v)
{
    if (v->type && v->type->destroy)
        v->type->destroy(v);
}

/** Copies via the type's copy function. */
cas_status_t value_copy(cas_t *cas, value_t *dst, const value_t *src)
{
    return src->type->copy(cas, dst, src);
}

/** Converts to a type of equal or higher rank. */
cas_status_t value_convert(cas_t *cas, value_t *dst, const value_t *src, const value_type_t *to)
{
    if (src->type == to)
        return value_copy(cas, dst, src);
    if (!to->promote_from || src->type->rank > to->rank)
        return cas_error(cas, CAS_ERR_TYPE, "cannot convert %s to %s", src->type->name, to->name);
    return to->promote_from(cas, dst, src);
}

/** Text form via the type. */
char *value_to_str(const value_t *v)
{
    return v->type->to_str(v);
}

/** Equal only if the types match and the type can compare. */
int value_equal(const value_t *a, const value_t *b)
{
    return a->type == b->type && a->type->cmp && a->type->cmp(a, b) == 0;
}

/** Promotes the lower-ranked operand, then runs the common type's kernel. */
cas_status_t value_arith(cas_t *cas, value_op_t op, value_t *dst, const value_t *a, const value_t *b)
{
    const value_type_t *common = a->type->rank >= b->type->rank ? a->type : b->type;
    value_binop_t kernel = common->binop[op];
    if (!kernel)
        return cas_error(cas, CAS_ERR_TYPE, "operation not supported for %s", common->name);

    // Use operands of the common type as they are, convert the other one
    // into a temporary.
    value_t tmp;
    const value_t *pa = a, *pb = b;
    cas_status_t st = CAS_OK;
    if (a->type != common) {
        if ((st = value_convert(cas, &tmp, a, common)) != CAS_OK)
            return st;
        pa = &tmp;
    } else if (b->type != common) {
        if ((st = value_convert(cas, &tmp, b, common)) != CAS_OK)
            return st;
        pb = &tmp;
    }

    st = kernel(cas, dst, pa, pb);
    if (pa == &tmp || pb == &tmp)
        value_destroy(&tmp);
    return st;
}

/** Negates through the type's neg kernel. */
cas_status_t value_negate(cas_t *cas, value_t *dst, const value_t *a)
{
    if (!a->type->neg)
        return cas_error(cas, CAS_ERR_TYPE, "cannot negate %s", a->type->name);
    return a->type->neg(cas, dst, a);
}
