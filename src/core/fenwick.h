#ifndef MONAC_CORE_FENWICK_H
#define MONAC_CORE_FENWICK_H

#include <stddef.h>
#include <stdint.h>

typedef struct mc_fenwick {
    uint32_t *values;
    uint32_t *bit;
    size_t count;
    size_t capacity;
    uint32_t total;
} mc_fenwick;

void mc_fenwick_init(mc_fenwick *fw);
void mc_fenwick_destroy(mc_fenwick *fw);

int mc_fenwick_reset(mc_fenwick *fw, size_t count, const uint32_t *initial_values);
int mc_fenwick_reset_uniform(mc_fenwick *fw, size_t count, uint32_t val);
void mc_fenwick_set(mc_fenwick *fw, size_t index, uint32_t new_val);
uint32_t mc_fenwick_get(const mc_fenwick *fw, size_t index);

/* Sum of values[0 .. count-1] */
uint32_t mc_fenwick_prefix_sum(const mc_fenwick *fw, size_t count);
uint32_t mc_fenwick_total(const mc_fenwick *fw);

/* Given 0-based view_line < total, find owning model_line (with val > 0) and sub_line offset */
int mc_fenwick_find_view_line(const mc_fenwick *fw, uint32_t view_line,
                              uint32_t *out_model_line, uint32_t *out_sub_line);

#endif /* MONAC_CORE_FENWICK_H */
