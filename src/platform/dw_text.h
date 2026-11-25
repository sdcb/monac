#ifndef MONAC_PLATFORM_DW_TEXT_H
#define MONAC_PLATFORM_DW_TEXT_H

#include "dwrite_c.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct mc_dw_context {
    MC_IDWriteFactory *factory;
    MC_IDWriteTextFormat *text_format;
    MC_IDWriteTextFormat *badge_format;
    wchar_t font_family[128];
    float font_size;
    float line_height;
    float baseline;
    float space_width;
    uint32_t tab_size;
    uint64_t font_version;
} mc_dw_context;

int mc_dw_init(mc_dw_context *ctx);
void mc_dw_destroy(mc_dw_context *ctx);

int mc_dw_set_font(mc_dw_context *ctx, const wchar_t *family,
                   float font_size, uint32_t tab_size);

MC_IDWriteTextLayout *mc_dw_create_layout(mc_dw_context *ctx,
                                          const wchar_t *text, uint32_t len,
                                          float max_width, bool word_wrap);

MC_IDWriteTextLayout *mc_dw_create_badge_layout(mc_dw_context *ctx,
                                                const wchar_t *text, uint32_t len);

int mc_dw_measure_text(mc_dw_context *ctx, MC_IDWriteTextFormat *fmt,
                       const wchar_t *text, uint32_t len,
                       float *out_width, float *out_height);

#endif /* MONAC_PLATFORM_DW_TEXT_H */
