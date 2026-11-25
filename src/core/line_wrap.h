#ifndef MONAC_CORE_LINE_WRAP_H
#define MONAC_CORE_LINE_WRAP_H

#include "../platform/dw_text.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define MC_DEFAULT_LONG_LINE_CHUNK 4096u
#define MC_DEFAULT_TOKENIZE_MAX_LINE 20000u

typedef struct mc_wrap_inline_t {
    uint32_t pos;
    MC_IDWriteInlineObject *object;
} mc_wrap_inline_t;

typedef struct mc_wrap_span_t {
    uint32_t start;
    uint32_t length;
    float indent_x;
    uint32_t chunk_index;
} mc_wrap_span_t;

typedef struct mc_wrap_chunk_t {
    uint32_t start;
    uint32_t length;
} mc_wrap_chunk_t;

typedef struct mc_wrap_result_t {
    mc_wrap_span_t *spans;
    size_t span_count;
    size_t span_cap;
    mc_wrap_chunk_t *chunks;
    size_t chunk_count;
    size_t chunk_cap;
    float wrap_width;
    mc_wrap_mode_t mode;
    uint64_t font_version;
} mc_wrap_result_t;

void mc_wrap_result_init(mc_wrap_result_t *res);
void mc_wrap_result_destroy(mc_wrap_result_t *res);

int mc_wrap_compute(mc_dw_context *dw,
                    const wchar_t *text, uint32_t length,
                    const mc_wrap_inline_t *inlines, size_t inline_count,
                    float max_width, mc_wrap_mode_t mode,
                    uint32_t chunk_size, mc_wrap_result_t *out);

#endif /* MONAC_CORE_LINE_WRAP_H */
