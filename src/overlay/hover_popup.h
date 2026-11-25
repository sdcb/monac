#ifndef MONAC_OVERLAY_HOVER_POPUP_H
#define MONAC_OVERLAY_HOVER_POPUP_H

#include "api/editor_internal.h"

struct mc_hover_state {
    bool visible;
    monac_pos_t anchor;
    wchar_t *text;
    size_t text_len;
    D2D1_RECT_F last_rect;
};

void mc_hover_init(monac_editor_t *ed);
void mc_hover_destroy(monac_editor_t *ed);
void mc_hover_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                   MC_ID2D1SolidColorBrush *brush);

#endif /* MONAC_OVERLAY_HOVER_POPUP_H */
