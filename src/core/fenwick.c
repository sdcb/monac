#include "fenwick.h"

#include <stdlib.h>
#include <string.h>

void mc_fenwick_init(mc_fenwick *fw)
{
    if (fw == NULL) return;
    memset(fw, 0, sizeof(*fw));
}

void mc_fenwick_destroy(mc_fenwick *fw)
{
    if (fw == NULL) return;
    free(fw->values);
    free(fw->bit);
    memset(fw, 0, sizeof(*fw));
}

static int mc_fenwick_reserve(mc_fenwick *fw, size_t count)
{
    size_t need = count + 1;
    uint32_t *v;
    uint32_t *b;
    if (need <= fw->capacity) return 1;
    v = (uint32_t *)realloc(fw->values, need * sizeof(uint32_t));
    if (v == NULL) return 0;
    fw->values = v;
    b = (uint32_t *)realloc(fw->bit, (need + 1) * sizeof(uint32_t));
    if (b == NULL) return 0;
    fw->bit = b;
    fw->capacity = need;
    return 1;
}

static void mc_fenwick_build_linear(mc_fenwick *fw)
{
    size_t i;
    fw->total = 0;
    if (fw->count == 0) return;
    memset(fw->bit, 0, (fw->count + 1) * sizeof(uint32_t));
    for (i = 1; i <= fw->count; ++i) {
        size_t parent = i + (i & (~i + 1));
        fw->bit[i] += fw->values[i - 1];
        fw->total += fw->values[i - 1];
        if (parent <= fw->count) {
            fw->bit[parent] += fw->bit[i];
        }
    }
}

int mc_fenwick_reset(mc_fenwick *fw, size_t count, const uint32_t *initial_values)
{
    if (fw == NULL) return 0;
    if (!mc_fenwick_reserve(fw, count)) return 0;
    fw->count = count;
    if (count > 0) {
        if (initial_values != NULL) {
            memcpy(fw->values, initial_values, count * sizeof(uint32_t));
        } else {
            memset(fw->values, 0, count * sizeof(uint32_t));
        }
    }
    mc_fenwick_build_linear(fw);
    return 1;
}

int mc_fenwick_reset_uniform(mc_fenwick *fw, size_t count, uint32_t val)
{
    size_t i;
    if (fw == NULL) return 0;
    if (!mc_fenwick_reserve(fw, count)) return 0;
    fw->count = count;
    for (i = 0; i < count; ++i) fw->values[i] = val;
    mc_fenwick_build_linear(fw);
    return 1;
}

void mc_fenwick_set(mc_fenwick *fw, size_t index, uint32_t new_val)
{
    int64_t delta;
    size_t i;
    if (fw == NULL || index >= fw->count) return;
    delta = (int64_t)new_val - (int64_t)fw->values[index];
    if (delta == 0) return;
    fw->values[index] = new_val;
    fw->total = (uint32_t)((int64_t)fw->total + delta);
    for (i = index + 1; i <= fw->count; i += (i & (~i + 1))) {
        fw->bit[i] = (uint32_t)((int64_t)fw->bit[i] + delta);
    }
}

uint32_t mc_fenwick_get(const mc_fenwick *fw, size_t index)
{
    if (fw == NULL || index >= fw->count) return 0;
    return fw->values[index];
}

uint32_t mc_fenwick_prefix_sum(const mc_fenwick *fw, size_t count)
{
    uint32_t sum = 0;
    size_t i;
    if (fw == NULL || fw->count == 0) return 0;
    if (count > fw->count) count = fw->count;
    for (i = count; i > 0; i -= (i & (~i + 1))) {
        sum += fw->bit[i];
    }
    return sum;
}

uint32_t mc_fenwick_total(const mc_fenwick *fw)
{
    return fw == NULL ? 0 : fw->total;
}

int mc_fenwick_find_view_line(const mc_fenwick *fw, uint32_t view_line,
                              uint32_t *out_model_line, uint32_t *out_sub_line)
{
    size_t pos = 0;
    uint32_t acc = 0;
    size_t step = 1;
    if (fw == NULL || fw->count == 0 || view_line >= fw->total) return 0;
    while ((step << 1) <= fw->count) step <<= 1;
    for (; step > 0; step >>= 1) {
        size_t next = pos + step;
        if (next <= fw->count && acc + fw->bit[next] <= view_line) {
            pos = next;
            acc += fw->bit[next];
        }
    }
    if (pos >= fw->count) return 0;
    if (out_model_line != NULL) *out_model_line = (uint32_t)pos;
    if (out_sub_line != NULL) *out_sub_line = view_line - acc;
    return 1;
}
