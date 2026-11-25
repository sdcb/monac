#ifndef MONAC_PLATFORM_INLINE_OBJECT_H
#define MONAC_PLATFORM_INLINE_OBJECT_H

#include "d2d_c.h"
#include "dw_text.h"

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

typedef enum mc_inline_kind_t {
    MC_INLINE_FOLD_BADGE = 0,
    MC_INLINE_INLAY = 1
} mc_inline_kind_t;

/* Shared per-frame D2D drawing context pointed to by inline objects during rendering. */
typedef struct mc_inline_draw_state_t {
    MC_ID2D1RenderTarget *rt;
    MC_ID2D1SolidColorBrush *brush;
    uint32_t hovered_fold_id;
    float origin_offset_x;
    float origin_offset_y;
    D2D1_DRAW_TEXT_OPTIONS text_options;
} mc_inline_draw_state_t;

typedef struct mc_inline_badge mc_inline_badge;

MC_IDWriteInlineObject *mc_inline_badge_create(mc_dw_context *dw,
                                               mc_inline_kind_t kind,
                                               uint32_t fold_id,
                                               const wchar_t *text,
                                               uint32_t fg_color,
                                               uint32_t bg_color,
                                               uint32_t border_color,
                                               mc_inline_draw_state_t *draw_state);

float mc_inline_badge_width(const MC_IDWriteInlineObject *obj);
uint32_t mc_inline_badge_fold_id(const MC_IDWriteInlineObject *obj);

#endif /* MONAC_PLATFORM_INLINE_OBJECT_H */
