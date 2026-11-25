#ifndef MONAC_OVERLAY_SUGGEST_POPUP_H
#define MONAC_OVERLAY_SUGGEST_POPUP_H

#include "api/editor_internal.h"

typedef struct mc_suggest_item_owned {
    wchar_t *label;
    wchar_t *detail;
    wchar_t *insert_text;
    monac_completion_kind_t kind;
    bool has_text_edit;
    monac_range_t edit_range;
} mc_suggest_item_owned;

struct mc_suggest_state {
    bool visible;
    monac_pos_t anchor;
    mc_suggest_item_owned *items;
    size_t item_count;
    size_t *filtered;
    size_t filtered_count;
    size_t filtered_cap;
    size_t selected;
    size_t scroll_top;
    D2D1_RECT_F last_rect;
};

void mc_suggest_init(monac_editor_t *ed);
void mc_suggest_destroy(monac_editor_t *ed);
void mc_suggest_refilter(monac_editor_t *ed);
void mc_suggest_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                     MC_ID2D1SolidColorBrush *brush);
bool mc_suggest_handle_keydown(monac_editor_t *ed, WPARAM vk);
bool mc_suggest_handle_mouse(monac_editor_t *ed, UINT msg,
                             float x, float y, int wheel_delta);

#endif /* MONAC_OVERLAY_SUGGEST_POPUP_H */
