#include "inline_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct mc_inline_badge {
    const MC_IDWriteInlineObjectVtbl *lpVtbl;
    volatile LONG ref_count;
    mc_inline_kind_t kind;
    uint32_t fold_id;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t border_color;
    float width;
    float height;
    float baseline;
    float pad_x;
    float text_height;
    MC_IDWriteTextLayout *label_layout;
    mc_inline_draw_state_t *draw_state;
};

static D2D1_COLOR_F mc_argb_to_d2d(uint32_t argb)
{
    D2D1_COLOR_F c;
    c.a = (float)((argb >> 24) & 0xFFu) / 255.0f;
    c.r = (float)((argb >> 16) & 0xFFu) / 255.0f;
    c.g = (float)((argb >> 8) & 0xFFu) / 255.0f;
    c.b = (float)(argb & 0xFFu) / 255.0f;
    return c;
}

static HRESULT STDMETHODCALLTYPE mc_badge_qi(MC_IDWriteInlineObject *This,
                                             REFIID riid, void **ppv)
{
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IDWriteInlineObject)) {
        *ppv = This;
        MC_CALL0(This, AddRef);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mc_badge_addref(MC_IDWriteInlineObject *This)
{
    mc_inline_badge *self = (mc_inline_badge *)This;
    return (ULONG)InterlockedIncrement(&self->ref_count);
}

static ULONG STDMETHODCALLTYPE mc_badge_release(MC_IDWriteInlineObject *This)
{
    mc_inline_badge *self = (mc_inline_badge *)This;
    ULONG count = (ULONG)InterlockedDecrement(&self->ref_count);
    if (count == 0) {
        MC_SAFE_RELEASE(self->label_layout);
        free(self);
    }
    return count;
}

static HRESULT STDMETHODCALLTYPE mc_badge_draw(MC_IDWriteInlineObject *This,
                                               void *clientDrawingContext,
                                               MC_IDWriteTextRenderer *renderer,
                                               FLOAT originX, FLOAT originY,
                                               BOOL isSideways, BOOL isRightToLeft,
                                               MC_IUnknown *clientDrawingEffect)
{
    mc_inline_badge *self = (mc_inline_badge *)This;
    mc_inline_draw_state_t *ds = self->draw_state;
    MC_UNUSED(clientDrawingContext);
    MC_UNUSED(renderer);
    MC_UNUSED(isSideways);
    MC_UNUSED(isRightToLeft);
    MC_UNUSED(clientDrawingEffect);

    if (ds == NULL || ds->rt == NULL || ds->brush == NULL) return S_OK;

    {
        float x = originX + ds->origin_offset_x;
        float y = originY + ds->origin_offset_y;
        bool hovered = (self->kind == MC_INLINE_FOLD_BADGE &&
                        self->fold_id != 0 &&
                        self->fold_id == ds->hovered_fold_id);
        uint32_t bg = hovered ? 0xFF3A5A8Cu : self->bg_color;
        uint32_t border = hovered ? 0xFF6BA6FFu : self->border_color;
        uint32_t fg = hovered ? 0xFFFFFFFFu : self->fg_color;
        D2D1_ROUNDED_RECT rr;
        D2D1_COLOR_F col;

        rr.rect.left = x + 1.0f;
        rr.rect.top = y + 1.5f;
        rr.rect.right = x + self->width - 1.0f;
        rr.rect.bottom = y + self->height - 1.5f;
        rr.radiusX = 4.0f;
        rr.radiusY = 4.0f;

        if ((bg >> 24) != 0) {
            col = mc_argb_to_d2d(bg);
            MC_CALL(ds->brush, SetColor, &col);
            MC_CALL(ds->rt, FillRoundedRectangle, &rr, MC_AS(ID2D1Brush, ds->brush));
        }
        if ((border >> 24) != 0) {
            col = mc_argb_to_d2d(border);
            MC_CALL(ds->brush, SetColor, &col);
            MC_CALL(ds->rt, DrawRoundedRectangle, &rr, MC_AS(ID2D1Brush, ds->brush),
                    1.0f, NULL);
        }
        if (self->label_layout != NULL) {
            D2D1_POINT_2F pt;
            pt.x = x + self->pad_x;
            pt.y = y + (self->height - self->text_height) * 0.5f;
            col = mc_argb_to_d2d(fg);
            MC_CALL(ds->brush, SetColor, &col);
            MC_CALL(ds->rt, DrawTextLayout, pt, self->label_layout,
                    MC_AS(ID2D1Brush, ds->brush), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_badge_get_metrics(MC_IDWriteInlineObject *This,
                                                      DWRITE_INLINE_OBJECT_METRICS *metrics)
{
    mc_inline_badge *self = (mc_inline_badge *)This;
    if (metrics == NULL) return E_POINTER;
    metrics->width = self->width;
    metrics->height = self->height;
    metrics->baseline = self->baseline;
    metrics->supportsSideways = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_badge_get_overhang(MC_IDWriteInlineObject *This,
                                                       DWRITE_OVERHANG_METRICS *overhangs)
{
    MC_UNUSED(This);
    if (overhangs == NULL) return E_POINTER;
    overhangs->left = 0.0f;
    overhangs->top = 0.0f;
    overhangs->right = 0.0f;
    overhangs->bottom = 0.0f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_badge_get_break(MC_IDWriteInlineObject *This,
                                                    DWRITE_BREAK_CONDITION *before,
                                                    DWRITE_BREAK_CONDITION *after)
{
    MC_UNUSED(This);
    if (before != NULL) *before = DWRITE_BREAK_CONDITION_CAN_BREAK;
    if (after != NULL) *after = DWRITE_BREAK_CONDITION_CAN_BREAK;
    return S_OK;
}

static const MC_IDWriteInlineObjectVtbl mc_inline_badge_vtbl = {
    mc_badge_qi,
    mc_badge_addref,
    mc_badge_release,
    mc_badge_draw,
    mc_badge_get_metrics,
    mc_badge_get_overhang,
    mc_badge_get_break
};

MC_IDWriteInlineObject *mc_inline_badge_create(mc_dw_context *dw,
                                               mc_inline_kind_t kind,
                                               uint32_t fold_id,
                                               const wchar_t *text,
                                               uint32_t fg_color,
                                               uint32_t bg_color,
                                               uint32_t border_color,
                                               mc_inline_draw_state_t *draw_state)
{
    mc_inline_badge *obj;
    const wchar_t *label = (text != NULL && text[0] != L'\0') ? text : L"\x2026";
    uint32_t len = (uint32_t)wcslen(label);
    DWRITE_TEXT_METRICS tm;

    if (dw == NULL) return NULL;
    obj = (mc_inline_badge *)calloc(1, sizeof(*obj));
    if (obj == NULL) return NULL;

    obj->lpVtbl = &mc_inline_badge_vtbl;
    obj->ref_count = 1;
    obj->kind = kind;
    obj->fold_id = fold_id;
    obj->fg_color = fg_color != 0 ? fg_color
                  : (kind == MC_INLINE_FOLD_BADGE ? 0xFFD8DEE9u : 0xFF9AA5B1u);
    obj->bg_color = bg_color != 0 ? bg_color
                  : (kind == MC_INLINE_FOLD_BADGE ? 0xFF2C3E55u : 0xFF262A34u);
    obj->border_color = border_color != 0 ? border_color
                      : (kind == MC_INLINE_FOLD_BADGE ? 0xFF4C6A92u : 0xFF3A4150u);
    obj->pad_x = (kind == MC_INLINE_FOLD_BADGE) ? 6.0f : 5.0f;
    obj->height = dw->line_height;
    obj->baseline = dw->baseline;
    obj->draw_state = draw_state;

    obj->label_layout = mc_dw_create_badge_layout(dw, label, len);
    memset(&tm, 0, sizeof(tm));
    if (obj->label_layout != NULL &&
        SUCCEEDED(MC_CALL(obj->label_layout, GetMetrics, &tm))) {
        obj->width = ceilf(tm.widthIncludingTrailingWhitespace + obj->pad_x * 2.0f);
        obj->text_height = tm.height;
    } else {
        obj->width = ceilf((float)len * dw->space_width + obj->pad_x * 2.0f);
        obj->text_height = dw->font_size;
    }
    if (obj->width < 16.0f) obj->width = 16.0f;
    return (MC_IDWriteInlineObject *)obj;
}

float mc_inline_badge_width(const MC_IDWriteInlineObject *obj)
{
    const mc_inline_badge *self = (const mc_inline_badge *)obj;
    return self == NULL ? 0.0f : self->width;
}

uint32_t mc_inline_badge_fold_id(const MC_IDWriteInlineObject *obj)
{
    const mc_inline_badge *self = (const mc_inline_badge *)obj;
    return self == NULL ? 0 : self->fold_id;
}
