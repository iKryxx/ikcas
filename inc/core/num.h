//
// Created by ikryxxdev on 2/10/26.
//

#ifndef IKCAS_NUM_H
#define IKCAS_NUM_H
#include <stdint.h>

typedef struct {int64_t num, den;} rat_t;

rat_t rat_from_i64(int64_t num);
bool rat_is_zero(rat_t rat);

rat_t rat_norm(rat_t a);
rat_t rat_add(rat_t a, rat_t b);
rat_t rat_sub(rat_t a, rat_t b);
rat_t rat_mul(rat_t a, rat_t b);
rat_t rat_div(rat_t a, rat_t b, bool *ok);

rat_t rat_pow_i64(rat_t a, int64_t e, bool* ok);

void rat_to_str(rat_t rat, char* buf, int cap);

#endif //IKCAS_NUM_H