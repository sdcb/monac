#ifndef MONAC_BASE_VEC_H
#define MONAC_BASE_VEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static inline int mc_vec_reserve(void **data, size_t *capacity,
                                 size_t element_size, size_t needed)
{
    size_t next;
    void *resized;
    if (data == NULL || capacity == NULL || element_size == 0 ||
        needed > SIZE_MAX / element_size) return 0;
    if (needed <= *capacity) return 1;
    next = *capacity == 0 ? 8 : *capacity;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    resized = realloc(*data, next * element_size);
    if (resized == NULL) return 0;
    *data = resized;
    *capacity = next;
    return 1;
}

#define MC_VEC_RESERVE(vec, needed) \
    mc_vec_reserve((void **)&(vec)->data, &(vec)->capacity, \
                   sizeof(*(vec)->data), (needed))

#endif
