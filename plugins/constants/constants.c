#include "arith/arith.h"
#include "engine/cas.h"
#include "expr/fwd.h"
#include "expr/op.h"

/** == TEXTS ========== */
#define PI_TEXT                                                                \
    "3.14159265358979323846264338327950288419716939937510"                     \
    "58209749445923078164062862089986280348253421170679"

#define E_TEXT                                                                 \
    "2.71828182845904523536028747135266249775724709369995"                     \
    "95749669676277240766303535475945713821785251664274"

#define PHI_TEXT                                                               \
    "1.61803398874989484820458683436563811772030917980576"                     \
    "28621354486227052604628189024497072072041893911374"

#define CONST_MAX_BITS 320


/** == MAKE FUNCTIONS ========== */

/**
 * Makes pi as a float at the session's precision. Sessions with more than
 * CONST_MAX_BITS get CONST_MAX_BITS, since the digits above run out there.
 */
static cas_status_t pi_make(cas_t *cas, value_t *out)
{
    size_t bits = cas->opts.float_bits;
    if (bits > CONST_MAX_BITS)
        bits = CONST_MAX_BITS;

    out->type = &arith_type_float;
    bigfloat_init(&out->as.f, bits);
    if (bigfloat_from_str(&out->as.f, PI_TEXT, bits) != 0) {
        bigfloat_free(&out->as.f);
        return cas_error(cas, CAS_ERR_INTERNAL, "cannot build pi");
    }
    return CAS_OK;
}

/**
 * Makes e as a float at the session's precision. Sessions with more than
 * CONST_MAX_BITS get CONST_MAX_BITS, since the digits above run out there.
 */
static cas_status_t e_make(cas_t *cas, value_t *out)
{
    size_t bits = cas->opts.float_bits;
    if (bits > CONST_MAX_BITS)
        bits = CONST_MAX_BITS;

    out->type = &arith_type_float;
    bigfloat_init(&out->as.f, bits);
    if (bigfloat_from_str(&out->as.f, E_TEXT, bits) != 0) {
        bigfloat_free(&out->as.f);
        return cas_error(cas, CAS_ERR_INTERNAL, "cannot build e");
    }
    return CAS_OK;
}

/**
 * Makes phi as a float at the session's precision. Sessions with more than
 * CONST_MAX_BITS get CONST_MAX_BITS, since the digits above run out there.
 */
static cas_status_t phi_make(cas_t *cas, value_t *out)
{
    size_t bits = cas->opts.float_bits;
    if (bits > CONST_MAX_BITS)
        bits = CONST_MAX_BITS;

    out->type = &arith_type_float;
    bigfloat_init(&out->as.f, bits);
    if (bigfloat_from_str(&out->as.f, PHI_TEXT, bits) != 0) {
        bigfloat_free(&out->as.f);
        return cas_error(cas, CAS_ERR_INTERNAL, "cannot build phi");
    }
    return CAS_OK;
}
/** == COLLECT ========== */

static const constant_def_t const_pi   = { "pi",   pi_make };
static const constant_def_t const_e    = { "e",    e_make };
static const constant_def_t const_phi  = { "phi",  phi_make };

static const constant_def_t *const constants[] = { &const_pi, &const_e, &const_phi };

/** Registers every constant in the table. */
static cas_status_t constants_init(cas_t *cas)
{
    cas_status_t status = CAS_OK;
    for (size_t i = 0; status == CAS_OK && i < sizeof(constants) / sizeof(*constants); i++)
        status = registry_add_constant(cas, constants[i]);
    return status;
}

IKCAS_PLUGIN(constants, "1.0", constants_init, NULL);
