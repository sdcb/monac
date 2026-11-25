#ifndef MONAC_OVERLAY_OVERLAY_H
#define MONAC_OVERLAY_OVERLAY_H

#include "api/editor_internal.h"
#include "suggest_popup.h"
#include "hover_popup.h"
#include "find_bar.h"

static inline D2D1_COLOR_F mc_ov_color(uint32_t argb)
{
    D2D1_COLOR_F c;
    c.a = (float)((argb >> 24) & 0xFFu) / 255.0f;
    c.r = (float)((argb >> 16) & 0xFFu) / 255.0f;
    c.g = (float)((argb >> 8) & 0xFFu) / 255.0f;
    c.b = (float)(argb & 0xFFu) / 255.0f;
    return c;
}

static inline bool mc_pt_in_rect(float x, float y, D2D1_RECT_F rc)
{
    return x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom;
}

#endif /* MONAC_OVERLAY_OVERLAY_H */
