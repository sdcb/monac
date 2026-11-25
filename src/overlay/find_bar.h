#ifndef MONAC_OVERLAY_FIND_BAR_H
#define MONAC_OVERLAY_FIND_BAR_H

#include "api/editor_internal.h"

struct mc_find_bar_state {
    bool open;
    bool with_replace;
    int active_field; /* 0 = find, 1 = replace */
    mc_wbuf find_text;
    mc_wbuf replace_text;
    bool case_sensitive;
    bool whole_word;
    D2D1_RECT_F bar_rect;
    D2D1_RECT_F find_box;
    D2D1_RECT_F replace_box;
    D2D1_RECT_F btn_case;
    D2D1_RECT_F btn_word;
    D2D1_RECT_F btn_prev;
    D2D1_RECT_F btn_next;
    D2D1_RECT_F btn_close;
    D2D1_RECT_F btn_rep;
    D2D1_RECT_F btn_rep_all;
};

void mc_find_bar_init(monac_editor_t *ed);
void mc_find_bar_destroy(monac_editor_t *ed);
void mc_find_bar_refresh_matches(monac_editor_t *ed);
void mc_find_bar_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                      MC_ID2D1SolidColorBrush *brush);
bool mc_find_bar_handle_keydown(monac_editor_t *ed, WPARAM vk, bool ctrl, bool shift);
bool mc_find_bar_handle_char(monac_editor_t *ed, wchar_t ch);
bool mc_find_bar_handle_mouse(monac_editor_t *ed, UINT msg, float x, float y);
void mc_find_bar_insert_text(monac_editor_t *ed, const wchar_t *text, size_t len);
bool mc_find_bar_ime_rect(const monac_editor_t *ed, monac_rect_f_t *out_rect);

#endif /* MONAC_OVERLAY_FIND_BAR_H */
