#include "overlay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void mc_hover_init(monac_editor_t *ed)
{
    if (ed == NULL) return;
    ed->hover = (mc_hover_state *)calloc(1, sizeof(mc_hover_state));
}

void mc_hover_destroy(monac_editor_t *ed)
{
    if (ed == NULL || ed->hover == NULL) return;
    free(ed->hover->text);
    free(ed->hover);
    ed->hover = NULL;
}

void mc_hover_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                   MC_ID2D1SolidColorBrush *brush)
{
    mc_hover_state *st;
    MC_IDWriteTextFormat *fmt;
    MC_IDWriteTextLayout *layout = NULL;
    DWRITE_TEXT_METRICS tm;
    monac_rect_f_t anchor_rc = { 0 };
    float max_w;
    float pad = 8.0f;
    float bw, bh, x, y;
    D2D1_ROUNDED_RECT rr;
    D2D1_COLOR_F col;
    D2D1_POINT_2F pt;
    HRESULT hr;

    if (ed == NULL || ed->hover == NULL || !ed->hover->visible ||
        ed->hover->text == NULL || ed->hover->text_len == 0) {
        return;
    }
    st = ed->hover;
    fmt = ed->doc.dw.badge_format != NULL ? ed->doc.dw.badge_format : ed->doc.dw.text_format;
    if (fmt == NULL || ed->doc.dw.factory == NULL) return;

    max_w = ed->viewport_w - 40.0f;
    if (max_w > 460.0f) max_w = 460.0f;
    if (max_w < 120.0f) max_w = 120.0f;

    hr = MC_CALL(ed->doc.dw.factory, CreateTextLayout,
                 st->text, (UINT32)st->text_len, fmt, max_w, 260.0f, &layout);
    if (FAILED(hr) || layout == NULL) return;
    (void)MC_CALL(layout, SetWordWrapping, DWRITE_WORD_WRAPPING_WRAP);

    memset(&tm, 0, sizeof(tm));
    (void)MC_CALL(layout, GetMetrics, &tm);
    bw = ceilf(tm.widthIncludingTrailingWhitespace) + pad * 2.0f;
    bh = ceilf(tm.height) + pad * 2.0f;
    if (bw < 60.0f) bw = 60.0f;
    if (bh < 26.0f) bh = 26.0f;

    if (!mc_renderer_pos_to_client_rect(ed, st->anchor, &anchor_rc)) {
        anchor_rc.left = mc_editor_text_left(ed);
        anchor_rc.top = 24.0f;
        anchor_rc.bottom = 44.0f;
    }

    x = anchor_rc.left;
    y = anchor_rc.top - bh - 4.0f;
    if (y < 4.0f) y = anchor_rc.bottom + 4.0f;
    if (x + bw > ed->viewport_w - 8.0f) x = ed->viewport_w - bw - 8.0f;
    if (x < 4.0f) x = 4.0f;

    st->last_rect.left = x;
    st->last_rect.top = y;
    st->last_rect.right = x + bw;
    st->last_rect.bottom = y + bh;

    rr.rect = st->last_rect;
    rr.radiusX = 5.0f;
    rr.radiusY = 5.0f;

    col = mc_ov_color(0xF5181A28u);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, FillRoundedRectangle, &rr, MC_AS(ID2D1Brush, brush));

    col = mc_ov_color(0xFF585B70u);
    MC_CALL(brush, SetColor, &col);
    MC_CALL(rt, DrawRoundedRectangle, &rr, MC_AS(ID2D1Brush, brush), 1.0f, NULL);

    col = mc_ov_color(0xFFE6EDF3u);
    MC_CALL(brush, SetColor, &col);
    pt.x = x + pad;
    pt.y = y + pad;
    MC_CALL(rt, DrawTextLayout, pt, layout, MC_AS(ID2D1Brush, brush),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);

    MC_SAFE_RELEASE(layout);
}

MONAC_API void monac_hover_show(monac_editor_t *ed, monac_pos_t anchor,
                                const wchar_t *text)
{
    mc_hover_state *st;
    size_t len;
    wchar_t *copy;
    if (ed == NULL || ed->hover == NULL) return;
    st = ed->hover;
    free(st->text);
    st->text = NULL;
    st->text_len = 0;

    if (text == NULL || text[0] == L'\0') {
        st->visible = false;
        mc_editor_invalidate_view(ed);
        return;
    }
    len = wcslen(text);
    copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) return;
    memcpy(copy, text, (len + 1) * sizeof(wchar_t));
    st->text = copy;
    st->text_len = len;
    st->anchor = anchor;
    st->visible = true;
    mc_editor_invalidate_view(ed);
}

MONAC_API void monac_hover_hide(monac_editor_t *ed)
{
    if (ed == NULL || ed->hover == NULL) return;
    if (ed->hover->visible) {
        ed->hover->visible = false;
        mc_editor_invalidate_view(ed);
    }
}

MONAC_API bool monac_hover_is_visible(const monac_editor_t *ed)
{
    return ed != NULL && ed->hover != NULL && ed->hover->visible;
}

MONAC_API size_t monac_hover_get_text(const monac_editor_t *ed,
                                      wchar_t *buf, size_t cap)
{
    if (ed == NULL || ed->hover == NULL || !ed->hover->visible ||
        ed->hover->text == NULL) {
        return 0;
    }
    if (buf != NULL && cap > 0) {
        size_t n = ed->hover->text_len < cap - 1 ? ed->hover->text_len : cap - 1;
        if (n > 0) memcpy(buf, ed->hover->text, n * sizeof(wchar_t));
        buf[n] = L'\0';
    }
    return ed->hover->text_len;
}
