#include "overlay.h"
#include "base/vec.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define MC_SUGGEST_MAX_VISIBLE 8u
#define MC_SUGGEST_ROW_H 22.0f
#define MC_SUGGEST_WIDTH 380.0f

static wchar_t *mc_sdup(const wchar_t *s)
{
    size_t len;
    wchar_t *copy;
    if (s == NULL) return NULL;
    len = wcslen(s);
    copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) return NULL;
    memcpy(copy, s, (len + 1) * sizeof(wchar_t));
    return copy;
}

static void mc_suggest_free_items(mc_suggest_state *st)
{
    size_t i;
    for (i = 0; i < st->item_count; ++i) {
        free(st->items[i].label);
        free(st->items[i].detail);
        free(st->items[i].insert_text);
    }
    free(st->items);
    st->items = NULL;
    st->item_count = 0;
    st->filtered_count = 0;
    st->selected = 0;
    st->scroll_top = 0;
}

void mc_suggest_init(monac_editor_t *ed)
{
    if (ed == NULL) return;
    ed->suggest = (mc_suggest_state *)calloc(1, sizeof(mc_suggest_state));
}

void mc_suggest_destroy(monac_editor_t *ed)
{
    if (ed == NULL || ed->suggest == NULL) return;
    mc_suggest_free_items(ed->suggest);
    free(ed->suggest->filtered);
    free(ed->suggest);
    ed->suggest = NULL;
}

static int mc_get_typed_prefix(const monac_editor_t *ed, mc_wbuf *out)
{
    mc_suggest_state *st = ed->suggest;
    mc_selection_t pri;
    size_t a_off = 0, c_off = 0;
    mc_wbuf_clear(out);
    if (st == NULL || !st->visible) return 0;
    pri = mc_cursors_primary(&ed->doc.cursors);
    if (pri.active.line != st->anchor.line || pri.active.col < st->anchor.col)
        return 0;
    if (!mc_pt_pos_to_offset(&ed->doc.tree, (mc_pos_t){ st->anchor.line, st->anchor.col }, &a_off))
        return 0;
    if (!mc_pt_pos_to_offset(&ed->doc.tree, pri.active, &c_off) || c_off < a_off)
        return 0;
    return mc_pt_get_range(&ed->doc.tree, a_off, c_off - a_off, out);
}

static bool mc_prefix_matches(const wchar_t *label, const wchar_t *prefix, size_t plen)
{
    size_t i, j = 0;
    if (plen == 0) return true;
    if (label == NULL) return false;
    for (i = 0; label[i] != L'\0' && j < plen; ++i) {
        if (towlower((wint_t)label[i]) == towlower((wint_t)prefix[j])) {
            ++j;
        }
    }
    return j == plen;
}

void mc_suggest_refilter(monac_editor_t *ed)
{
    mc_suggest_state *st;
    mc_wbuf prefix;
    size_t i;
    if (ed == NULL || ed->suggest == NULL || !ed->suggest->visible) return;
    st = ed->suggest;
    mc_wbuf_init(&prefix);
    (void)mc_get_typed_prefix(ed, &prefix);

    st->filtered_count = 0;
    for (i = 0; i < st->item_count; ++i) {
        if (mc_prefix_matches(st->items[i].label, prefix.data, prefix.length)) {
            if (mc_vec_reserve((void **)&st->filtered, &st->filtered_cap,
                               sizeof(size_t), st->filtered_count + 1)) {
                st->filtered[st->filtered_count++] = i;
            }
        }
    }
    mc_wbuf_destroy(&prefix);

    if (st->filtered_count == 0) {
        st->selected = 0;
        st->scroll_top = 0;
    } else if (st->selected >= st->filtered_count) {
        st->selected = 0;
        st->scroll_top = 0;
    }
}

static void mc_kind_badge_info(monac_completion_kind_t kind, wchar_t *ch, uint32_t *color)
{
    switch (kind) {
    case MONAC_COMPLETION_METHOD:
    case MONAC_COMPLETION_FUNCTION:
    case MONAC_COMPLETION_CONSTRUCTOR:
        *ch = L'M'; *color = 0xFFCBA6F7u; break;
    case MONAC_COMPLETION_PROPERTY:
    case MONAC_COMPLETION_FIELD:
        *ch = L'P'; *color = 0xFF89B4FAu; break;
    case MONAC_COMPLETION_CLASS:
    case MONAC_COMPLETION_INTERFACE:
    case MONAC_COMPLETION_MODULE:
        *ch = L'C'; *color = 0xFFF9E2AFu; break;
    case MONAC_COMPLETION_KEYWORD:
        *ch = L'K'; *color = 0xFFF38BA8u; break;
    case MONAC_COMPLETION_VARIABLE:
        *ch = L'V'; *color = 0xFF94E2D5u; break;
    default:
        *ch = L'T'; *color = 0xFFA6ADC8u; break;
    }
}

void mc_suggest_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                     MC_ID2D1SolidColorBrush *brush)
{
    mc_suggest_state *st;
    monac_rect_f_t anchor_rc = { 0 };
    size_t vis_n;
    float w = MC_SUGGEST_WIDTH;
    float h;
    float x, y;
    size_t i;
    D2D1_ROUNDED_RECT bg;
    D2D1_COLOR_F col;
    MC_IDWriteTextFormat *fmt;

    if (ed == NULL || ed->suggest == NULL || !ed->suggest->visible) return;
    st = ed->suggest;
    if (st->filtered_count == 0) return;

    vis_n = st->filtered_count < MC_SUGGEST_MAX_VISIBLE
                ? st->filtered_count
                : MC_SUGGEST_MAX_VISIBLE;
    h = (float)vis_n * MC_SUGGEST_ROW_H + 8.0f;

    if (!mc_renderer_pos_to_client_rect(ed, st->anchor, &anchor_rc)) {
        anchor_rc.left = mc_editor_text_left(ed);
        anchor_rc.top = 20.0f;
        anchor_rc.bottom = 40.0f;
    }

    x = anchor_rc.left;
    y = anchor_rc.bottom + 2.0f;
    if (y + h > ed->viewport_h - 4.0f && anchor_rc.top - h - 2.0f >= 4.0f) {
        y = anchor_rc.top - h - 2.0f;
    }
    if (x + w > ed->viewport_w - 8.0f) x = ed->viewport_w - w - 8.0f;
    if (x < 4.0f) x = 4.0f;

    st->last_rect.left = x;
    st->last_rect.top = y;
    st->last_rect.right = x + w;
    st->last_rect.bottom = y + h;

    bg.rect = st->last_rect;
    bg.radiusX = 5.0f;
    bg.radiusY = 5.0f;

    col = mc_ov_color(0xF51E2030u);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, FillRoundedRectangle, &bg, MC_AS(ID2D1Brush, brush));

    col = mc_ov_color(0xFF45475Au);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, DrawRoundedRectangle, &bg, MC_AS(ID2D1Brush, brush), 1.0f, NULL);

    fmt = ed->doc.dw.badge_format != NULL ? ed->doc.dw.badge_format : ed->doc.dw.text_format;
    if (fmt == NULL) return;

    for (i = 0; i < vis_n; ++i) {
        size_t fidx = st->scroll_top + i;
        const mc_suggest_item_owned *item;
        float ry = y + 4.0f + (float)i * MC_SUGGEST_ROW_H;
        wchar_t kch = L'T';
        uint32_t kcol = 0xFFA6ADC8u;
        D2D1_RECT_F row_rc = { x + 3.0f, ry, x + w - 3.0f, ry + MC_SUGGEST_ROW_H };

        if (fidx >= st->filtered_count) break;
        item = &st->items[st->filtered[fidx]];

        if (fidx == st->selected) {
            D2D1_ROUNDED_RECT sel_rr = { row_rc, 3.0f, 3.0f };
            col = mc_ov_color(0xFF313D5Au);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, FillRoundedRectangle, &sel_rr, MC_AS(ID2D1Brush, brush));
        }

        /* Kind badge */
        mc_kind_badge_info(item->kind, &kch, &kcol);
        {
            D2D1_ROUNDED_RECT krr;
            D2D1_RECT_F ktrc;
            krr.rect.left = x + 8.0f;
            krr.rect.top = ry + 3.0f;
            krr.rect.right = x + 24.0f;
            krr.rect.bottom = ry + MC_SUGGEST_ROW_H - 3.0f;
            krr.radiusX = 3.0f;
            krr.radiusY = 3.0f;
            col = mc_ov_color((kcol & 0x00FFFFFFu) | 0x33000000u);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, FillRoundedRectangle, &krr, MC_AS(ID2D1Brush, brush));

            col = mc_ov_color(kcol);
            MC_CALL(brush, SetColor, &col);
            ktrc.left = krr.rect.left + 3.0f;
            ktrc.top = ry + 2.0f;
            ktrc.right = krr.rect.right;
            ktrc.bottom = ry + MC_SUGGEST_ROW_H;
            MC_CALL(rt, DrawText, &kch, 1, fmt, &ktrc,
                    MC_AS(ID2D1Brush, brush), D2D1_DRAW_TEXT_OPTIONS_NONE,
                    DWRITE_MEASURING_MODE_NATURAL);
        }

        /* Label */
        if (item->label != NULL) {
            D2D1_RECT_F lrc = { x + 30.0f, ry + 2.0f, x + 165.0f, ry + MC_SUGGEST_ROW_H };
            col = mc_ov_color(fidx == st->selected ? 0xFFFFFFFFu : 0xFFCDD6F4u);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, DrawText, item->label, (UINT32)wcslen(item->label), fmt, &lrc,
                    MC_AS(ID2D1Brush, brush),
                    mc_editor_text_options(ed, D2D1_DRAW_TEXT_OPTIONS_CLIP),
                    DWRITE_MEASURING_MODE_NATURAL);
        }

        /* Detail (muted) */
        if (item->detail != NULL && item->detail[0] != L'\0') {
            D2D1_RECT_F drc = { x + 170.0f, ry + 2.0f, x + w - 10.0f, ry + MC_SUGGEST_ROW_H };
            col = mc_ov_color(0xFF7F849Cu);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, DrawText, item->detail, (UINT32)wcslen(item->detail), fmt, &drc,
                    MC_AS(ID2D1Brush, brush),
                    mc_editor_text_options(ed, D2D1_DRAW_TEXT_OPTIONS_CLIP),
                    DWRITE_MEASURING_MODE_NATURAL);
        }
    }

    /* Scrollbar if more items than visible */
    if (st->filtered_count > MC_SUGGEST_MAX_VISIBLE) {
        float track_h = h - 8.0f;
        float thumb_h = track_h * ((float)MC_SUGGEST_MAX_VISIBLE / (float)st->filtered_count);
        float max_top = (float)(st->filtered_count - MC_SUGGEST_MAX_VISIBLE);
        float ty = y + 4.0f + ((float)st->scroll_top / max_top) * (track_h - thumb_h);
        D2D1_ROUNDED_RECT srr;
        if (thumb_h < 14.0f) thumb_h = 14.0f;
        srr.rect.left = x + w - 6.0f;
        srr.rect.top = ty;
        srr.rect.right = x + w - 2.0f;
        srr.rect.bottom = ty + thumb_h;
        srr.radiusX = 2.0f;
        srr.radiusY = 2.0f;
        col = mc_ov_color(0xAA6C7086u);
        MC_CALL(brush, SetColor, &col);
        MC_CALL(rt, FillRoundedRectangle, &srr, MC_AS(ID2D1Brush, brush));
    }
}

bool mc_suggest_handle_keydown(monac_editor_t *ed, WPARAM vk)
{
    mc_suggest_state *st;
    if (ed == NULL || ed->suggest == NULL || !ed->suggest->visible) return false;
    st = ed->suggest;
    if (st->filtered_count == 0) {
        if (vk == VK_ESCAPE) {
            monac_suggest_hide(ed);
            return true;
        }
        return false;
    }

    switch (vk) {
    case VK_DOWN:
        st->selected = (st->selected + 1) % st->filtered_count;
        if (st->selected < st->scroll_top) st->scroll_top = st->selected;
        else if (st->selected >= st->scroll_top + MC_SUGGEST_MAX_VISIBLE)
            st->scroll_top = st->selected - MC_SUGGEST_MAX_VISIBLE + 1;
        return true;

    case VK_UP:
        st->selected = (st->selected == 0) ? (st->filtered_count - 1) : (st->selected - 1);
        if (st->selected < st->scroll_top) st->scroll_top = st->selected;
        else if (st->selected >= st->scroll_top + MC_SUGGEST_MAX_VISIBLE)
            st->scroll_top = st->selected - MC_SUGGEST_MAX_VISIBLE + 1;
        return true;

    case VK_RETURN:
    case VK_TAB:
        (void)monac_suggest_accept(ed);
        return true;

    case VK_ESCAPE:
        monac_suggest_hide(ed);
        return true;

    default:
        return false;
    }
}

bool mc_suggest_handle_mouse(monac_editor_t *ed, UINT msg,
                             float x, float y, int wheel_delta)
{
    mc_suggest_state *st;
    if (ed == NULL || ed->suggest == NULL || !ed->suggest->visible) return false;
    st = ed->suggest;
    if (!mc_pt_in_rect(x, y, st->last_rect)) {
        if (msg == WM_LBUTTONDOWN) monac_suggest_hide(ed);
        return false;
    }
    if (msg == WM_MOUSEWHEEL && st->filtered_count > MC_SUGGEST_MAX_VISIBLE) {
        size_t max_top = st->filtered_count - MC_SUGGEST_MAX_VISIBLE;
        if (wheel_delta > 0 && st->scroll_top > 0) --st->scroll_top;
        else if (wheel_delta < 0 && st->scroll_top < max_top) ++st->scroll_top;
        return true;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        int row = (int)((y - (st->last_rect.top + 4.0f)) / MC_SUGGEST_ROW_H);
        if (row >= 0 && st->scroll_top + (size_t)row < st->filtered_count) {
            st->selected = st->scroll_top + (size_t)row;
            if (msg == WM_LBUTTONDBLCLK) {
                (void)monac_suggest_accept(ed);
            }
        }
        return true;
    }
    return true;
}

MONAC_API void monac_suggest_show(monac_editor_t *ed, monac_pos_t anchor,
                                  const monac_completion_item_t *items, size_t count)
{
    mc_suggest_state *st;
    size_t i;
    if (ed == NULL || ed->suggest == NULL) return;
    st = ed->suggest;
    mc_suggest_free_items(st);

    if (items == NULL || count == 0) {
        st->visible = false;
        mc_editor_invalidate_view(ed);
        return;
    }

    st->items = (mc_suggest_item_owned *)calloc(count, sizeof(mc_suggest_item_owned));
    if (st->items == NULL) return;
    for (i = 0; i < count; ++i) {
        st->items[i].label = mc_sdup(items[i].label ? items[i].label : L"");
        st->items[i].detail = mc_sdup(items[i].detail);
        st->items[i].insert_text = mc_sdup(items[i].insert_text ? items[i].insert_text : items[i].label);
        st->items[i].kind = items[i].kind;
        st->items[i].has_text_edit = items[i].has_text_edit;
        st->items[i].edit_range = items[i].edit_range;
    }
    st->item_count = count;
    st->anchor = anchor;
    st->visible = true;
    st->selected = 0;
    st->scroll_top = 0;
    mc_suggest_refilter(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API void monac_suggest_hide(monac_editor_t *ed)
{
    if (ed == NULL || ed->suggest == NULL) return;
    if (ed->suggest->visible) {
        ed->suggest->visible = false;
        mc_editor_invalidate_view(ed);
    }
}

MONAC_API bool monac_suggest_is_visible(const monac_editor_t *ed)
{
    return ed != NULL && ed->suggest != NULL &&
           ed->suggest->visible && ed->suggest->filtered_count > 0;
}

MONAC_API size_t monac_suggest_get_count(const monac_editor_t *ed)
{
    if (ed == NULL || ed->suggest == NULL || !ed->suggest->visible) return 0;
    return ed->suggest->filtered_count;
}

MONAC_API size_t monac_suggest_get_label(const monac_editor_t *ed, size_t index,
                                         wchar_t *buf, size_t cap)
{
    const wchar_t *lbl;
    size_t len;
    if (ed == NULL || ed->suggest == NULL || !ed->suggest->visible ||
        index >= ed->suggest->filtered_count) return 0;
    lbl = ed->suggest->items[ed->suggest->filtered[index]].label;
    if (lbl == NULL) return 0;
    len = wcslen(lbl);
    if (buf != NULL && cap > 0) {
        size_t n = len < cap - 1 ? len : cap - 1;
        if (n > 0) memcpy(buf, lbl, n * sizeof(wchar_t));
        buf[n] = L'\0';
    }
    return len;
}

MONAC_API bool monac_suggest_accept(monac_editor_t *ed)
{
    mc_suggest_state *st;
    const mc_suggest_item_owned *item;
    monac_text_edit_t edit;
    monac_pos_t new_pos;
    const wchar_t *ins;
    size_t ins_len;
    size_t start_off = 0;

    if (ed == NULL || ed->suggest == NULL || !ed->suggest->visible ||
        ed->suggest->filtered_count == 0) {
        return false;
    }
    st = ed->suggest;
    item = &st->items[st->filtered[st->selected]];
    ins = item->insert_text != NULL ? item->insert_text : (item->label ? item->label : L"");
    ins_len = wcslen(ins);

    if (item->has_text_edit) {
        edit.range = item->edit_range;
    } else {
        mc_selection_t pri = mc_cursors_primary(&ed->doc.cursors);
        edit.range.start = st->anchor;
        edit.range.end.line = pri.active.line;
        edit.range.end.col = pri.active.col;
        if (mc_pos_cmp((mc_pos_t){ edit.range.end.line, edit.range.end.col },
                       (mc_pos_t){ edit.range.start.line, edit.range.start.col }) < 0) {
            edit.range.end = edit.range.start;
        }
    }
    edit.text = ins;
    edit.len = ins_len;

    (void)mc_pt_pos_to_offset(&ed->doc.tree,
                              (mc_pos_t){ edit.range.start.line, edit.range.start.col },
                              &start_off);
    st->visible = false;
    monac_apply_edits(ed, &edit, 1);

    {
        mc_pos_t mp = { 0, 0 };
        (void)mc_pt_offset_to_pos(&ed->doc.tree, start_off + ins_len, &mp);
        new_pos.line = mp.line;
        new_pos.col = mp.col;
    }
    {
        monac_selection_t sel = { new_pos, new_pos };
        monac_set_selections(ed, &sel, 1);
    }
    return true;
}
