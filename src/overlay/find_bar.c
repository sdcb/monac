#include "overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mc_find_bar_init(monac_editor_t *ed)
{
    mc_find_bar_state *fb;
    if (ed == NULL) return;
    fb = (mc_find_bar_state *)calloc(1, sizeof(mc_find_bar_state));
    if (fb != NULL) {
        mc_wbuf_init(&fb->find_text);
        mc_wbuf_init(&fb->replace_text);
    }
    ed->find_bar = fb;
}

void mc_find_bar_destroy(monac_editor_t *ed)
{
    if (ed == NULL || ed->find_bar == NULL) return;
    mc_wbuf_destroy(&ed->find_bar->find_text);
    mc_wbuf_destroy(&ed->find_bar->replace_text);
    free(ed->find_bar);
    ed->find_bar = NULL;
}

void mc_find_bar_refresh_matches(monac_editor_t *ed)
{
    mc_find_bar_state *fb;
    mc_search_options_t opts;
    if (ed == NULL || ed->find_bar == NULL) return;
    fb = ed->find_bar;
    opts.case_sensitive = fb->case_sensitive;
    opts.whole_word = fb->whole_word;
    (void)mc_search_update(&ed->doc.search, &ed->doc.tree,
                           fb->find_text.data ? fb->find_text.data : L"",
                           fb->find_text.length, opts);
    mc_search_apply_decorations(&ed->doc.search, &ed->doc.decorations, MC_OWNER_SEARCH);
    mc_editor_invalidate_view(ed);
}

static void mc_find_bar_jump(monac_editor_t *ed, bool next)
{
    mc_selection_t pri = mc_cursors_primary(&ed->doc.cursors);
    mc_range_t pr = mc_selection_range(pri);
    size_t from = 0;
    mc_search_match_t m;
    int ok;

    if (next) {
        (void)mc_pt_pos_to_offset(&ed->doc.tree, pr.end, &from);
        ok = mc_search_find_next(&ed->doc.search, from, true, &m);
    } else {
        (void)mc_pt_pos_to_offset(&ed->doc.tree, pr.start, &from);
        ok = mc_search_find_prev(&ed->doc.search, from, true, &m);
    }
    if (ok) {
        mc_search_apply_decorations(&ed->doc.search, &ed->doc.decorations, MC_OWNER_SEARCH);
        mc_cursors_set_single(&ed->doc.cursors, m.range.start, m.range.end);
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
    }
}

static void mc_draw_btn(MC_ID2D1RenderTarget *rt, MC_ID2D1SolidColorBrush *brush,
                        MC_IDWriteTextFormat *fmt, D2D1_RECT_F rc,
                        const wchar_t *label, bool active)
{
    D2D1_ROUNDED_RECT rr = { rc, 3.0f, 3.0f };
    D2D1_COLOR_F col;
    col = mc_ov_color(active ? 0xFF3E5A8Au : 0xFF2B2D3Du);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, FillRoundedRectangle, &rr, MC_AS(ID2D1Brush, brush));

    col = mc_ov_color(active ? 0xFF89B4FAu : 0xFF45475Au);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, DrawRoundedRectangle, &rr, MC_AS(ID2D1Brush, brush), 1.0f, NULL);

    col = mc_ov_color(active ? 0xFFFFFFFFu : 0xFFCDD6F4u);
    MC_CALL(brush, SetColor, &col);
    (void)MC_CALL(fmt, SetTextAlignment, DWRITE_TEXT_ALIGNMENT_CENTER);
    MC_CALL(rt, DrawText, label, (UINT32)wcslen(label), fmt, &rc,
            MC_AS(ID2D1Brush, brush), D2D1_DRAW_TEXT_OPTIONS_CLIP,
            DWRITE_MEASURING_MODE_NATURAL);
    (void)MC_CALL(fmt, SetTextAlignment, DWRITE_TEXT_ALIGNMENT_LEADING);
}

void mc_find_bar_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                      MC_ID2D1SolidColorBrush *brush)
{
    mc_find_bar_state *fb;
    MC_IDWriteTextFormat *fmt;
    float w = 390.0f;
    float h;
    float x, y = 8.0f;
    D2D1_ROUNDED_RECT bg;
    D2D1_COLOR_F col;
    wchar_t count_buf[32];

    if (ed == NULL || ed->find_bar == NULL || !ed->find_bar->open) return;
    fb = ed->find_bar;
    fmt = ed->doc.dw.badge_format != NULL ? ed->doc.dw.badge_format : ed->doc.dw.text_format;
    if (fmt == NULL) return;

    if (w > ed->viewport_w - 24.0f) w = ed->viewport_w - 24.0f;
    if (w < 240.0f) w = 240.0f;
    h = fb->with_replace ? 68.0f : 36.0f;
    x = ed->viewport_w - mc_editor_scrollbar_width() - w - 8.0f;
    if (x < 8.0f) x = 8.0f;

    fb->bar_rect = (D2D1_RECT_F){ x, y, x + w, y + h };
    bg.rect = fb->bar_rect;
    bg.radiusX = 5.0f;
    bg.radiusY = 5.0f;

    col = mc_ov_color(0xF51E2030u);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, FillRoundedRectangle, &bg, MC_AS(ID2D1Brush, brush));

    col = mc_ov_color(0xFF45475Au);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, DrawRoundedRectangle, &bg, MC_AS(ID2D1Brush, brush), 1.0f, NULL);

    /* Row 1: Find input + match count + Aa + W + Prev + Next + Close */
    fb->find_box = (D2D1_RECT_F){ x + 8.0f, y + 6.0f, x + w - 186.0f, y + 30.0f };
    fb->btn_case = (D2D1_RECT_F){ x + w - 122.0f, y + 7.0f, x + w - 98.0f, y + 29.0f };
    fb->btn_word = (D2D1_RECT_F){ x + w - 94.0f, y + 7.0f, x + w - 74.0f, y + 29.0f };
    fb->btn_prev = (D2D1_RECT_F){ x + w - 70.0f, y + 7.0f, x + w - 50.0f, y + 29.0f };
    fb->btn_next = (D2D1_RECT_F){ x + w - 46.0f, y + 7.0f, x + w - 26.0f, y + 29.0f };
    fb->btn_close = (D2D1_RECT_F){ x + w - 22.0f, y + 7.0f, x + w - 6.0f, y + 29.0f };

    /* Find input box */
    {
        D2D1_ROUNDED_RECT ir = { fb->find_box, 3.0f, 3.0f };
        D2D1_RECT_F trc = { fb->find_box.left + 5.0f, fb->find_box.top + 3.0f,
                            fb->find_box.right - 4.0f, fb->find_box.bottom - 2.0f };
        col = mc_ov_color(0xFF141520u);
        MC_CALL(brush, SetColor, &col);
        MC_CALL(rt, FillRoundedRectangle, &ir, MC_AS(ID2D1Brush, brush));
        col = mc_ov_color(fb->active_field == 0 ? 0xFF89B4FAu : 0xFF3E4155u);
        MC_CALL(brush, SetColor, &col);
        MC_CALL(rt, DrawRoundedRectangle, &ir, MC_AS(ID2D1Brush, brush), 1.0f, NULL);

        if (fb->find_text.length > 0) {
            col = mc_ov_color(0xFFCDD6F4u);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, DrawText, fb->find_text.data, (UINT32)fb->find_text.length, fmt,
                    &trc, MC_AS(ID2D1Brush, brush),
                    mc_editor_text_options(ed, D2D1_DRAW_TEXT_OPTIONS_CLIP),
                    DWRITE_MEASURING_MODE_NATURAL);
        } else {
            col = mc_ov_color(0xFF6C7086u);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, DrawText, L"Find", 4, fmt, &trc,
                    MC_AS(ID2D1Brush, brush), D2D1_DRAW_TEXT_OPTIONS_CLIP,
                    DWRITE_MEASURING_MODE_NATURAL);
        }
    }

    /* Match counter */
    {
        D2D1_RECT_F crc = { x + w - 182.0f, y + 9.0f, x + w - 126.0f, y + 29.0f };
        if (fb->find_text.length == 0) {
            count_buf[0] = L'\0';
        } else if (ed->doc.search.match_count == 0) {
            wcscpy(count_buf, L"0/0");
        } else {
            swprintf(count_buf, 32, L"%zu/%zu",
                     ed->doc.search.current_index + 1u,
                     ed->doc.search.match_count);
        }
        col = mc_ov_color(ed->doc.search.match_count > 0 ? 0xFFA6ADC8u : 0xFFF38BA8u);
        MC_CALL(brush, SetColor, &col);
        MC_CALL(rt, DrawText, count_buf, (UINT32)wcslen(count_buf), fmt, &crc,
                MC_AS(ID2D1Brush, brush), D2D1_DRAW_TEXT_OPTIONS_CLIP,
                DWRITE_MEASURING_MODE_NATURAL);
    }

    mc_draw_btn(rt, brush, fmt, fb->btn_case, L"Aa", fb->case_sensitive);
    mc_draw_btn(rt, brush, fmt, fb->btn_word, L"W", fb->whole_word);
    mc_draw_btn(rt, brush, fmt, fb->btn_prev, L"\x2191", false);
    mc_draw_btn(rt, brush, fmt, fb->btn_next, L"\x2193", false);
    mc_draw_btn(rt, brush, fmt, fb->btn_close, L"\x00D7", false);

    /* Row 2: Replace input + Rep + All */
    if (fb->with_replace) {
        fb->replace_box = (D2D1_RECT_F){ x + 8.0f, y + 36.0f, x + w - 126.0f, y + 60.0f };
        fb->btn_rep = (D2D1_RECT_F){ x + w - 122.0f, y + 37.0f, x + w - 66.0f, y + 59.0f };
        fb->btn_rep_all = (D2D1_RECT_F){ x + w - 62.0f, y + 37.0f, x + w - 6.0f, y + 59.0f };
        {
            D2D1_ROUNDED_RECT ir = { fb->replace_box, 3.0f, 3.0f };
            D2D1_RECT_F trc = { fb->replace_box.left + 5.0f, fb->replace_box.top + 3.0f,
                                fb->replace_box.right - 4.0f, fb->replace_box.bottom - 2.0f };
            col = mc_ov_color(0xFF141520u);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, FillRoundedRectangle, &ir, MC_AS(ID2D1Brush, brush));
            col = mc_ov_color(fb->active_field == 1 ? 0xFF89B4FAu : 0xFF3E4155u);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, DrawRoundedRectangle, &ir, MC_AS(ID2D1Brush, brush), 1.0f, NULL);

            if (fb->replace_text.length > 0) {
                col = mc_ov_color(0xFFCDD6F4u);
                MC_CALL(brush, SetColor, &col);
                MC_CALL(rt, DrawText, fb->replace_text.data, (UINT32)fb->replace_text.length, fmt,
                        &trc, MC_AS(ID2D1Brush, brush),
                        mc_editor_text_options(ed, D2D1_DRAW_TEXT_OPTIONS_CLIP),
                        DWRITE_MEASURING_MODE_NATURAL);
            } else {
                col = mc_ov_color(0xFF6C7086u);
                MC_CALL(brush, SetColor, &col);
                MC_CALL(rt, DrawText, L"Replace", 7, fmt, &trc,
                        MC_AS(ID2D1Brush, brush), D2D1_DRAW_TEXT_OPTIONS_CLIP,
                        DWRITE_MEASURING_MODE_NATURAL);
            }
        }
        mc_draw_btn(rt, brush, fmt, fb->btn_rep, L"Rep", false);
        mc_draw_btn(rt, brush, fmt, fb->btn_rep_all, L"All", false);
    }
}

void mc_find_bar_insert_text(monac_editor_t *ed, const wchar_t *text, size_t len)
{
    mc_find_bar_state *fb;
    mc_wbuf *dst;
    if (ed == NULL || ed->find_bar == NULL || !ed->find_bar->open || text == NULL)
        return;
    fb = ed->find_bar;
    dst = (fb->active_field == 1 && fb->with_replace) ? &fb->replace_text : &fb->find_text;
    (void)mc_wbuf_append(dst, text, len);
    if (fb->active_field == 0) mc_find_bar_refresh_matches(ed);
    else mc_editor_invalidate_view(ed);
}

bool mc_find_bar_ime_rect(const monac_editor_t *ed, monac_rect_f_t *out_rect)
{
    const mc_find_bar_state *fb;
    D2D1_RECT_F box;
    if (ed == NULL || ed->find_bar == NULL || !ed->find_bar->open || out_rect == NULL)
        return false;
    fb = ed->find_bar;
    box = (fb->active_field == 1 && fb->with_replace) ? fb->replace_box : fb->find_box;
    out_rect->left = box.left + 6.0f;
    out_rect->top = box.top + 2.0f;
    out_rect->right = box.right;
    out_rect->bottom = box.bottom;
    return true;
}

bool mc_find_bar_handle_keydown(monac_editor_t *ed, WPARAM vk, bool ctrl, bool shift)
{
    mc_find_bar_state *fb;
    if (ed == NULL || ed->find_bar == NULL || !ed->find_bar->open) return false;
    fb = ed->find_bar;

    if (ctrl && vk == 'F') {
        fb->active_field = 0;
        return true;
    }
    if (ctrl && vk == 'H') {
        fb->with_replace = true;
        fb->active_field = 1;
        return true;
    }

    switch (vk) {
    case VK_ESCAPE:
        monac_find_bar_close(ed);
        return true;

    case VK_TAB:
        if (fb->with_replace) fb->active_field = 1 - fb->active_field;
        return true;

    case VK_RETURN:
        if (fb->active_field == 0) {
            mc_find_bar_jump(ed, !shift);
        } else if (fb->with_replace) {
            if (mc_search_replace_current(&ed->doc.search, &ed->doc.edit_stack,
                                          &ed->doc.cursors,
                                          fb->replace_text.data ? fb->replace_text.data : L"",
                                          fb->replace_text.length, GetTickCount64())) {
                mc_find_bar_refresh_matches(ed);
                mc_editor_notify_text_changed(ed, mc_doc_version(&ed->doc), 0);
            }
        }
        return true;

    case VK_BACK: {
        mc_wbuf *dst = (fb->active_field == 1 && fb->with_replace)
                           ? &fb->replace_text
                           : &fb->find_text;
        if (dst->length > 0) {
            --dst->length;
            dst->data[dst->length] = L'\0';
            if (fb->active_field == 0) mc_find_bar_refresh_matches(ed);
        }
        return true;
    }

    default:
        break;
    }
    return false;
}

bool mc_find_bar_handle_char(monac_editor_t *ed, wchar_t ch)
{
    if (ed == NULL || ed->find_bar == NULL || !ed->find_bar->open) return false;
    if (ch < 0x20u || ch == 0x7Fu) return true;
    mc_find_bar_insert_text(ed, &ch, 1);
    return true;
}

bool mc_find_bar_handle_mouse(monac_editor_t *ed, UINT msg, float x, float y)
{
    mc_find_bar_state *fb;
    if (ed == NULL || ed->find_bar == NULL || !ed->find_bar->open) return false;
    fb = ed->find_bar;
    if (!mc_pt_in_rect(x, y, fb->bar_rect)) return false;

    if (msg == WM_LBUTTONDOWN) {
        if (mc_pt_in_rect(x, y, fb->btn_close)) {
            monac_find_bar_close(ed);
        } else if (mc_pt_in_rect(x, y, fb->btn_case)) {
            fb->case_sensitive = !fb->case_sensitive;
            mc_find_bar_refresh_matches(ed);
        } else if (mc_pt_in_rect(x, y, fb->btn_word)) {
            fb->whole_word = !fb->whole_word;
            mc_find_bar_refresh_matches(ed);
        } else if (mc_pt_in_rect(x, y, fb->btn_prev)) {
            mc_find_bar_jump(ed, false);
        } else if (mc_pt_in_rect(x, y, fb->btn_next)) {
            mc_find_bar_jump(ed, true);
        } else if (mc_pt_in_rect(x, y, fb->find_box)) {
            fb->active_field = 0;
        } else if (fb->with_replace && mc_pt_in_rect(x, y, fb->replace_box)) {
            fb->active_field = 1;
        } else if (fb->with_replace && mc_pt_in_rect(x, y, fb->btn_rep)) {
            if (mc_search_replace_current(&ed->doc.search, &ed->doc.edit_stack,
                                          &ed->doc.cursors,
                                          fb->replace_text.data ? fb->replace_text.data : L"",
                                          fb->replace_text.length, GetTickCount64())) {
                mc_find_bar_refresh_matches(ed);
                mc_editor_notify_text_changed(ed, mc_doc_version(&ed->doc), 0);
            }
        } else if (fb->with_replace && mc_pt_in_rect(x, y, fb->btn_rep_all)) {
            if (mc_search_replace_all(&ed->doc.search, &ed->doc.edit_stack,
                                      &ed->doc.cursors,
                                      fb->replace_text.data ? fb->replace_text.data : L"",
                                      fb->replace_text.length, GetTickCount64()) > 0) {
                mc_find_bar_refresh_matches(ed);
                mc_editor_notify_text_changed(ed, mc_doc_version(&ed->doc), 0);
            }
        }
    }
    return true;
}

MONAC_API void monac_find_bar_open(monac_editor_t *ed, bool with_replace)
{
    if (ed == NULL || ed->find_bar == NULL) return;
    ed->find_bar->open = true;
    ed->find_bar->with_replace = with_replace;
    ed->find_bar->active_field = 0;
    mc_find_bar_refresh_matches(ed);
    mc_ime_update_position(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API void monac_find_bar_close(monac_editor_t *ed)
{
    if (ed == NULL || ed->find_bar == NULL) return;
    ed->find_bar->open = false;
    mc_dec_remove_by_owner(&ed->doc.decorations, MC_OWNER_SEARCH);
    mc_ime_update_position(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API bool monac_find_bar_is_open(const monac_editor_t *ed)
{
    return ed != NULL && ed->find_bar != NULL && ed->find_bar->open;
}

MONAC_API void monac_find_bar_set_query(monac_editor_t *ed, const wchar_t *query)
{
    if (ed == NULL || ed->find_bar == NULL) return;
    mc_wbuf_clear(&ed->find_bar->find_text);
    if (query != NULL && query[0] != L'\0') {
        (void)mc_wbuf_append_wcstr(&ed->find_bar->find_text, query);
    }
    mc_find_bar_refresh_matches(ed);
}

MONAC_API size_t monac_find_bar_get_match_count(const monac_editor_t *ed)
{
    if (ed == NULL) return 0;
    return ed->doc.search.match_count;
}

/* ---- Overlay coordinator hooks called by editor / renderer / win32_host ---- */

void mc_overlays_init(monac_editor_t *ed)
{
    mc_suggest_init(ed);
    mc_hover_init(ed);
    mc_find_bar_init(ed);
}

void mc_overlays_destroy(monac_editor_t *ed)
{
    mc_find_bar_destroy(ed);
    mc_hover_destroy(ed);
    mc_suggest_destroy(ed);
}

void mc_overlays_on_text_changed(monac_editor_t *ed)
{
    if (ed == NULL) return;
    if (ed->suggest != NULL && ed->suggest->visible) {
        mc_suggest_refilter(ed);
    }
    if (ed->find_bar != NULL && ed->find_bar->open) {
        mc_find_bar_refresh_matches(ed);
    }
}

void mc_overlays_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                      MC_ID2D1SolidColorBrush *brush)
{
    /* Draw order per plan 6.1 #9: Find Bar -> Suggest Popup -> Hover Popup */
    mc_find_bar_draw(ed, rt, brush);
    mc_suggest_draw(ed, rt, brush);
    mc_hover_draw(ed, rt, brush);
}

bool mc_overlays_handle_keydown(monac_editor_t *ed, WPARAM vk,
                                bool ctrl, bool shift, bool alt)
{
    MC_UNUSED(alt);
    if (mc_suggest_handle_keydown(ed, vk)) return true;
    if (mc_find_bar_handle_keydown(ed, vk, ctrl, shift)) return true;
    return false;
}

bool mc_overlays_handle_char(monac_editor_t *ed, wchar_t ch)
{
    if (mc_find_bar_handle_char(ed, ch)) return true;
    return false;
}

bool mc_overlays_handle_mouse(monac_editor_t *ed, UINT msg,
                              float x, float y, int wheel_delta)
{
    if (mc_suggest_handle_mouse(ed, msg, x, y, wheel_delta)) return true;
    if (mc_find_bar_handle_mouse(ed, msg, x, y)) return true;
    return false;
}

bool mc_overlays_has_text_focus(const monac_editor_t *ed)
{
    return ed != NULL && ed->find_bar != NULL && ed->find_bar->open;
}

bool mc_overlays_ime_rect(const monac_editor_t *ed, monac_rect_f_t *out_rect)
{
    return mc_find_bar_ime_rect(ed, out_rect);
}

void mc_overlays_commit_ime(monac_editor_t *ed, const wchar_t *text, size_t len)
{
    mc_find_bar_insert_text(ed, text, len);
}
