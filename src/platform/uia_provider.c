#define CINTERFACE
#define COBJMACROS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <oleauto.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
#include <uiautomationclient.h>

#include "uia_provider.h"
#include "base/utf.h"

#include <stdlib.h>
#include <string.h>

static const IID MC_IID_IRawElementProviderSimple = {
    0xd6dd68d1u, 0x86fdu, 0x4332u,
    { 0x86u, 0x66u, 0x9au, 0xbeu, 0xdeu, 0xa2u, 0xd2u, 0x4cu }
};
static const IID MC_IID_ITextProvider = {
    0x3589c92cu, 0x63f3u, 0x4367u,
    { 0x99u, 0xbbu, 0xadu, 0xa6u, 0x53u, 0xb7u, 0x7cu, 0xf2u }
};
static const IID MC_IID_ITextRangeProvider = {
    0x5347ad7bu, 0xc355u, 0x46f8u,
    { 0xafu, 0xf5u, 0x90u, 0x90u, 0x33u, 0x58u, 0x2fu, 0x63u }
};

struct mc_uia_provider {
    IRawElementProviderSimple simple_iface;
    ITextProvider text_iface;
    volatile LONG ref_count;
    monac_editor_t *ed;
    HWND hwnd;
};

typedef struct mc_uia_range {
    ITextRangeProvider iface;
    volatile LONG ref_count;
    mc_uia_provider *owner;
    size_t start;
    size_t end;
} mc_uia_range;

typedef HRESULT (WINAPI *mc_uia_disconnect_fn)(IRawElementProviderSimple *);

static mc_uia_provider *mc_provider_from_text(ITextProvider *tp)
{
    return (mc_uia_provider *)((char *)tp - offsetof(mc_uia_provider, text_iface));
}

static size_t mc_clamp_off(const monac_editor_t *ed, size_t off)
{
    size_t len = mc_pt_length(&ed->doc.tree);
    return off <= len ? off : len;
}

static void mc_clamp_range(mc_uia_range *r)
{
    if (r->owner == NULL || r->owner->ed == NULL) return;
    r->start = mc_clamp_off(r->owner->ed, r->start);
    r->end = mc_clamp_off(r->owner->ed, r->end);
    if (r->start > r->end) r->start = r->end;
}

static ITextRangeProvider *mc_uia_range_create(mc_uia_provider *owner,
                                               size_t start, size_t end);

/* ---- ITextRangeProvider implementation ---- */

static HRESULT STDMETHODCALLTYPE mc_range_qi(ITextRangeProvider *This,
                                             REFIID riid, void **ppv)
{
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &MC_IID_ITextRangeProvider)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mc_range_addref(ITextRangeProvider *This)
{
    mc_uia_range *self = (mc_uia_range *)This;
    return (ULONG)InterlockedIncrement(&self->ref_count);
}

static ULONG STDMETHODCALLTYPE mc_range_release(ITextRangeProvider *This)
{
    mc_uia_range *self = (mc_uia_range *)This;
    ULONG n = (ULONG)InterlockedDecrement(&self->ref_count);
    if (n == 0) {
        if (self->owner != NULL) {
            self->owner->simple_iface.lpVtbl->Release(&self->owner->simple_iface);
        }
        free(self);
    }
    return n;
}

static HRESULT STDMETHODCALLTYPE mc_range_clone(ITextRangeProvider *This,
                                                ITextRangeProvider **pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = mc_uia_range_create(self->owner, self->start, self->end);
    return *pRetVal != NULL ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE mc_range_compare(ITextRangeProvider *This,
                                                  ITextRangeProvider *range,
                                                  BOOL *pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    mc_uia_range *other = (mc_uia_range *)range;
    if (pRetVal == NULL) return E_POINTER;
    if (other == NULL || other->iface.lpVtbl != self->iface.lpVtbl) {
        *pRetVal = FALSE;
        return S_OK;
    }
    mc_clamp_range(self);
    mc_clamp_range(other);
    *pRetVal = (self->owner == other->owner &&
                self->start == other->start &&
                self->end == other->end) ? TRUE : FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_compare_endpoints(
    ITextRangeProvider *This,
    enum TextPatternRangeEndpoint endpoint,
    ITextRangeProvider *targetRange,
    enum TextPatternRangeEndpoint targetEndpoint,
    int *pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    mc_uia_range *other = (mc_uia_range *)targetRange;
    size_t a, b;
    if (pRetVal == NULL) return E_POINTER;
    if (other == NULL || other->iface.lpVtbl != self->iface.lpVtbl)
        return E_INVALIDARG;
    mc_clamp_range(self);
    mc_clamp_range(other);
    a = (endpoint == TextPatternRangeEndpoint_Start) ? self->start : self->end;
    b = (targetEndpoint == TextPatternRangeEndpoint_Start) ? other->start : other->end;
    *pRetVal = (a < b) ? -1 : ((a > b) ? 1 : 0);
    return S_OK;
}

static size_t mc_step_unit_forward(const monac_editor_t *ed, size_t off,
                                   enum TextUnit unit, int count, int *out_moved)
{
    size_t len = mc_pt_length(&ed->doc.tree);
    int moved = 0;
    while (moved < count && off < len) {
        mc_pos_t pos = { 0, 0 };
        (void)mc_pt_offset_to_pos(&ed->doc.tree, off, &pos);
        if (unit == TextUnit_Character) {
            mc_pos_t np = mc_pos_move_right(&ed->doc.tree, pos);
            size_t noff = off;
            (void)mc_pt_pos_to_offset(&ed->doc.tree, np, &noff);
            if (noff <= off) break;
            off = noff;
        } else if (unit == TextUnit_Word) {
            mc_pos_t np = mc_pos_move_word_right(&ed->doc.tree, pos);
            size_t noff = off;
            (void)mc_pt_pos_to_offset(&ed->doc.tree, np, &noff);
            if (noff <= off) break;
            off = noff;
        } else if (unit == TextUnit_Line || unit == TextUnit_Paragraph) {
            size_t lc = mc_pt_line_count(&ed->doc.tree);
            if ((size_t)pos.line + 1 < lc) {
                off = mc_pt_line_start_offset(&ed->doc.tree, pos.line + 1u);
            } else {
                if (off == len) break;
                off = len;
            }
        } else {
            off = len;
        }
        ++moved;
    }
    if (out_moved != NULL) *out_moved = moved;
    return off;
}

static size_t mc_step_unit_backward(const monac_editor_t *ed, size_t off,
                                    enum TextUnit unit, int count, int *out_moved)
{
    int moved = 0;
    int target = -count;
    while (moved < target && off > 0) {
        mc_pos_t pos = { 0, 0 };
        (void)mc_pt_offset_to_pos(&ed->doc.tree, off, &pos);
        if (unit == TextUnit_Character) {
            mc_pos_t np = mc_pos_move_left(&ed->doc.tree, pos);
            size_t noff = off;
            (void)mc_pt_pos_to_offset(&ed->doc.tree, np, &noff);
            if (noff >= off) break;
            off = noff;
        } else if (unit == TextUnit_Word) {
            mc_pos_t np = mc_pos_move_word_left(&ed->doc.tree, pos);
            size_t noff = off;
            (void)mc_pt_pos_to_offset(&ed->doc.tree, np, &noff);
            if (noff >= off) break;
            off = noff;
        } else if (unit == TextUnit_Line || unit == TextUnit_Paragraph) {
            size_t ls = mc_pt_line_start_offset(&ed->doc.tree, pos.line);
            if (off > ls) {
                off = ls;
            } else if (pos.line > 0) {
                off = mc_pt_line_start_offset(&ed->doc.tree, pos.line - 1u);
            } else {
                break;
            }
        } else {
            off = 0;
        }
        ++moved;
    }
    if (out_moved != NULL) *out_moved = -moved;
    return off;
}

static HRESULT STDMETHODCALLTYPE mc_range_expand(ITextRangeProvider *This,
                                                 enum TextUnit unit)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    size_t len;
    mc_pos_t pos = { 0, 0 };

    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->owner->ed;
    mc_clamp_range(self);
    len = mc_pt_length(&ed->doc.tree);
    if (len == 0) {
        self->start = self->end = 0;
        return S_OK;
    }

    (void)mc_pt_offset_to_pos(&ed->doc.tree, self->start, &pos);
    switch (unit) {
    case TextUnit_Character:
        if (self->start < len) {
            mc_pos_t np = mc_pos_move_right(&ed->doc.tree, pos);
            (void)mc_pt_pos_to_offset(&ed->doc.tree, np, &self->end);
        } else {
            self->end = self->start;
        }
        break;
    case TextUnit_Word: {
        mc_range_t wr = mc_word_range_at(&ed->doc.tree, pos);
        (void)mc_pt_pos_to_offset(&ed->doc.tree, wr.start, &self->start);
        (void)mc_pt_pos_to_offset(&ed->doc.tree, wr.end, &self->end);
        break;
    }
    case TextUnit_Line:
    case TextUnit_Paragraph: {
        size_t lc = mc_pt_line_count(&ed->doc.tree);
        self->start = mc_pt_line_start_offset(&ed->doc.tree, pos.line);
        self->end = ((size_t)pos.line + 1 < lc)
                        ? mc_pt_line_start_offset(&ed->doc.tree, pos.line + 1u)
                        : len;
        break;
    }
    default:
        self->start = 0;
        self->end = len;
        break;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_find_attr(ITextRangeProvider *This,
                                                    TEXTATTRIBUTEID attrId,
                                                    VARIANT val, BOOL backward,
                                                    ITextRangeProvider **pRetVal)
{
    MC_UNUSED(This); MC_UNUSED(attrId); MC_UNUSED(val); MC_UNUSED(backward);
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_find_text(ITextRangeProvider *This,
                                                    BSTR text, BOOL backward,
                                                    BOOL ignoreCase,
                                                    ITextRangeProvider **pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    mc_search_state st;
    mc_search_options_t opts;
    size_t i;
    size_t qlen;
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (text == NULL) return S_OK;
    qlen = SysStringLen(text);
    if (qlen == 0) return S_OK;
    ed = self->owner->ed;
    mc_clamp_range(self);

    mc_search_init(&st);
    opts.case_sensitive = !ignoreCase;
    opts.whole_word = false;
    if (mc_search_update(&st, &ed->doc.tree, text, qlen, opts) && st.match_count > 0) {
        if (!backward) {
            for (i = 0; i < st.match_count; ++i) {
                if (st.matches[i].start_offset >= self->start &&
                    st.matches[i].end_offset <= self->end) {
                    *pRetVal = mc_uia_range_create(self->owner,
                                                   st.matches[i].start_offset,
                                                   st.matches[i].end_offset);
                    break;
                }
            }
        } else {
            for (i = st.match_count; i > 0; --i) {
                if (st.matches[i - 1].start_offset >= self->start &&
                    st.matches[i - 1].end_offset <= self->end) {
                    *pRetVal = mc_uia_range_create(self->owner,
                                                   st.matches[i - 1].start_offset,
                                                   st.matches[i - 1].end_offset);
                    break;
                }
            }
        }
    }
    mc_search_destroy(&st);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_get_attr(ITextRangeProvider *This,
                                                   TEXTATTRIBUTEID attrId,
                                                   VARIANT *pRetVal)
{
    IUnknown *not_sup = NULL;
    MC_UNUSED(This); MC_UNUSED(attrId);
    if (pRetVal == NULL) return E_POINTER;
    VariantInit(pRetVal);
    if (SUCCEEDED(UiaGetReservedNotSupportedValue(&not_sup)) && not_sup != NULL) {
        pRetVal->vt = VT_UNKNOWN;
        pRetVal->punkVal = not_sup;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_get_rects(ITextRangeProvider *This,
                                                    SAFEARRAY **pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    SAFEARRAY *sa;
    mc_pos_t ps = { 0, 0 }, pe = { 0, 0 };
    monac_rect_f_t rs = { 0 }, re = { 0 };
    POINT p0, p1;
    double *data = NULL;
    float scale;

    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->owner->ed;
    mc_clamp_range(self);

    sa = SafeArrayCreateVector(VT_R8, 0, 4);
    if (sa == NULL) return E_OUTOFMEMORY;

    (void)mc_pt_offset_to_pos(&ed->doc.tree, self->start, &ps);
    (void)mc_pt_offset_to_pos(&ed->doc.tree, self->end, &pe);
    (void)mc_renderer_pos_to_client_rect(ed, (monac_pos_t){ ps.line, ps.col }, &rs);
    (void)mc_renderer_pos_to_client_rect(ed, (monac_pos_t){ pe.line, pe.col }, &re);
    scale = (ed->dpi > 0.0f ? ed->dpi : 96.0f) / 96.0f;

    p0.x = (LONG)(rs.left * scale);
    p0.y = (LONG)(rs.top * scale);
    p1.x = (LONG)((re.right > rs.right ? re.right : rs.right) * scale);
    p1.y = (LONG)((re.bottom > rs.bottom ? re.bottom : rs.bottom) * scale);
    if (ed->hwnd != NULL) {
        ClientToScreen(ed->hwnd, &p0);
        ClientToScreen(ed->hwnd, &p1);
    }

    if (SUCCEEDED(SafeArrayAccessData(sa, (void **)&data)) && data != NULL) {
        data[0] = (double)p0.x;
        data[1] = (double)p0.y;
        data[2] = (double)(p1.x > p0.x ? (p1.x - p0.x) : 2);
        data[3] = (double)(p1.y > p0.y ? (p1.y - p0.y) : 16);
        SafeArrayUnaccessData(sa);
    }
    *pRetVal = sa;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_enclosing(ITextRangeProvider *This,
                                                    IRawElementProviderSimple **pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    if (pRetVal == NULL) return E_POINTER;
    if (self->owner == NULL) return UIA_E_ELEMENTNOTAVAILABLE;
    *pRetVal = &self->owner->simple_iface;
    (*pRetVal)->lpVtbl->AddRef(*pRetVal);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_get_text(ITextRangeProvider *This,
                                                   int maxLength, BSTR *pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    size_t want;
    mc_wbuf buf;
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->owner->ed;
    mc_clamp_range(self);

    want = self->end - self->start;
    if (maxLength >= 0 && (size_t)maxLength < want) want = (size_t)maxLength;
    if (want == 0) {
        *pRetVal = SysAllocStringLen(L"", 0);
        return *pRetVal ? S_OK : E_OUTOFMEMORY;
    }

    mc_wbuf_init(&buf);
    if (!mc_pt_get_range(&ed->doc.tree, self->start, want, &buf)) {
        mc_wbuf_destroy(&buf);
        return E_OUTOFMEMORY;
    }
    *pRetVal = SysAllocStringLen(buf.data, (UINT)buf.length);
    mc_wbuf_destroy(&buf);
    return *pRetVal != NULL ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE mc_range_move(ITextRangeProvider *This,
                                               enum TextUnit unit, int count,
                                               int *pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    int moved = 0;
    size_t old_span;

    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = 0;
    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->owner->ed;
    mc_clamp_range(self);
    if (count == 0) return S_OK;

    old_span = self->end - self->start;
    if (count > 0) {
        self->start = mc_step_unit_forward(ed, self->start, unit, count, &moved);
    } else {
        self->start = mc_step_unit_backward(ed, self->start, unit, count, &moved);
    }
    if (old_span == 0) {
        self->end = self->start;
    } else {
        self->end = self->start;
        (void)mc_range_expand(This, unit);
    }
    *pRetVal = moved;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_move_endpoint_unit(
    ITextRangeProvider *This,
    enum TextPatternRangeEndpoint endpoint,
    enum TextUnit unit, int count, int *pRetVal)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    int moved = 0;
    size_t pos;

    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = 0;
    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->owner->ed;
    mc_clamp_range(self);
    if (count == 0) return S_OK;

    pos = (endpoint == TextPatternRangeEndpoint_Start) ? self->start : self->end;
    if (count > 0) pos = mc_step_unit_forward(ed, pos, unit, count, &moved);
    else pos = mc_step_unit_backward(ed, pos, unit, count, &moved);

    if (endpoint == TextPatternRangeEndpoint_Start) {
        self->start = pos;
        if (self->start > self->end) self->end = self->start;
    } else {
        self->end = pos;
        if (self->end < self->start) self->start = self->end;
    }
    *pRetVal = moved;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_move_endpoint_range(
    ITextRangeProvider *This,
    enum TextPatternRangeEndpoint endpoint,
    ITextRangeProvider *targetRange,
    enum TextPatternRangeEndpoint targetEndpoint)
{
    mc_uia_range *self = (mc_uia_range *)This;
    mc_uia_range *other = (mc_uia_range *)targetRange;
    size_t val;
    if (other == NULL || other->iface.lpVtbl != self->iface.lpVtbl)
        return E_INVALIDARG;
    mc_clamp_range(self);
    mc_clamp_range(other);
    val = (targetEndpoint == TextPatternRangeEndpoint_Start) ? other->start : other->end;
    if (endpoint == TextPatternRangeEndpoint_Start) {
        self->start = val;
        if (self->start > self->end) self->end = self->start;
    } else {
        self->end = val;
        if (self->end < self->start) self->start = self->end;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_select(ITextRangeProvider *This)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    mc_pos_t ps = { 0, 0 }, pe = { 0, 0 };
    monac_selection_t sel;

    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->owner->ed;
    mc_clamp_range(self);
    (void)mc_pt_offset_to_pos(&ed->doc.tree, self->start, &ps);
    (void)mc_pt_offset_to_pos(&ed->doc.tree, self->end, &pe);
    sel.anchor.line = ps.line;
    sel.anchor.col = ps.col;
    sel.active.line = pe.line;
    sel.active.col = pe.col;
    monac_set_selections(ed, &sel, 1);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_add_sel(ITextRangeProvider *This)
{
    MC_UNUSED(This);
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE mc_range_remove_sel(ITextRangeProvider *This)
{
    MC_UNUSED(This);
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE mc_range_scroll(ITextRangeProvider *This,
                                                 BOOL alignToTop)
{
    mc_uia_range *self = (mc_uia_range *)This;
    monac_editor_t *ed;
    mc_pos_t ps = { 0, 0 };
    mc_view_pos_t vp = { 0, 0 };
    float lh;

    if (self->owner == NULL || self->owner->ed == NULL)
        return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->owner->ed;
    mc_clamp_range(self);
    (void)mc_pt_offset_to_pos(&ed->doc.tree, self->start, &ps);
    if (mc_proj_model_to_view(&ed->doc.projection, ps, &vp)) {
        lh = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
        if (alignToTop) {
            ed->scroll_y = (float)vp.view_line * lh;
        } else {
            float y = (float)vp.view_line * lh;
            ed->scroll_y = y > ed->viewport_h ? (y - ed->viewport_h + lh) : 0.0f;
        }
        if (ed->scroll_y < 0.0f) ed->scroll_y = 0.0f;
        mc_editor_invalidate_view(ed);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_range_get_children(ITextRangeProvider *This,
                                                       SAFEARRAY **pRetVal)
{
    MC_UNUSED(This);
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *pRetVal != NULL ? S_OK : E_OUTOFMEMORY;
}

static const ITextRangeProviderVtbl mc_uia_range_vtbl = {
    mc_range_qi,
    mc_range_addref,
    mc_range_release,
    mc_range_clone,
    mc_range_compare,
    mc_range_compare_endpoints,
    mc_range_expand,
    mc_range_find_attr,
    mc_range_find_text,
    mc_range_get_attr,
    mc_range_get_rects,
    mc_range_enclosing,
    mc_range_get_text,
    mc_range_move,
    mc_range_move_endpoint_unit,
    mc_range_move_endpoint_range,
    mc_range_select,
    mc_range_add_sel,
    mc_range_remove_sel,
    mc_range_scroll,
    mc_range_get_children
};

static ITextRangeProvider *mc_uia_range_create(mc_uia_provider *owner,
                                               size_t start, size_t end)
{
    mc_uia_range *r;
    if (owner == NULL) return NULL;
    r = (mc_uia_range *)calloc(1, sizeof(*r));
    if (r == NULL) return NULL;
    r->iface.lpVtbl = (ITextRangeProviderVtbl *)&mc_uia_range_vtbl;
    r->ref_count = 1;
    r->owner = owner;
    owner->simple_iface.lpVtbl->AddRef(&owner->simple_iface);
    r->start = start <= end ? start : end;
    r->end = start <= end ? end : start;
    mc_clamp_range(r);
    return &r->iface;
}

/* ---- IRawElementProviderSimple implementation ---- */

static HRESULT STDMETHODCALLTYPE mc_simple_qi(IRawElementProviderSimple *This,
                                              REFIID riid, void **ppv)
{
    mc_uia_provider *self = (mc_uia_provider *)This;
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &MC_IID_IRawElementProviderSimple)) {
        *ppv = &self->simple_iface;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    if (IsEqualIID(riid, &MC_IID_ITextProvider)) {
        *ppv = &self->text_iface;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mc_simple_addref(IRawElementProviderSimple *This)
{
    mc_uia_provider *self = (mc_uia_provider *)This;
    return (ULONG)InterlockedIncrement(&self->ref_count);
}

static ULONG STDMETHODCALLTYPE mc_simple_release(IRawElementProviderSimple *This)
{
    mc_uia_provider *self = (mc_uia_provider *)This;
    ULONG n = (ULONG)InterlockedDecrement(&self->ref_count);
    if (n == 0) {
        free(self);
    }
    return n;
}

static HRESULT STDMETHODCALLTYPE mc_simple_get_opts(IRawElementProviderSimple *This,
                                                    enum ProviderOptions *pRetVal)
{
    MC_UNUSED(This);
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = (enum ProviderOptions)(ProviderOptions_ServerSideProvider |
                                      ProviderOptions_UseComThreading);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_simple_get_pattern(IRawElementProviderSimple *This,
                                                       PATTERNID patternId,
                                                       IUnknown **pRetVal)
{
    mc_uia_provider *self = (mc_uia_provider *)This;
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (patternId == UIA_TextPatternId) {
        *pRetVal = (IUnknown *)&self->text_iface;
        This->lpVtbl->AddRef(This);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_simple_get_prop(IRawElementProviderSimple *This,
                                                    PROPERTYID propId,
                                                    VARIANT *pRetVal)
{
    mc_uia_provider *self = (mc_uia_provider *)This;
    if (pRetVal == NULL) return E_POINTER;
    VariantInit(pRetVal);
    if (self->ed == NULL) return UIA_E_ELEMENTNOTAVAILABLE;

    if (propId == UIA_ControlTypePropertyId) {
        pRetVal->vt = VT_I4;
        pRetVal->lVal = UIA_DocumentControlTypeId;
    } else if (propId == UIA_NamePropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"Monac Editor");
    } else if (propId == UIA_IsKeyboardFocusablePropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propId == UIA_HasKeyboardFocusPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = self->ed->has_focus ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (propId == UIA_IsTextPatternAvailablePropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_simple_get_host(IRawElementProviderSimple *This,
                                                    IRawElementProviderSimple **pRetVal)
{
    mc_uia_provider *self = (mc_uia_provider *)This;
    if (pRetVal == NULL) return E_POINTER;
    if (self->hwnd == NULL) {
        *pRetVal = NULL;
        return S_OK;
    }
    return UiaHostProviderFromHwnd(self->hwnd, pRetVal);
}

static const IRawElementProviderSimpleVtbl mc_uia_simple_vtbl = {
    mc_simple_qi,
    mc_simple_addref,
    mc_simple_release,
    mc_simple_get_opts,
    mc_simple_get_pattern,
    mc_simple_get_prop,
    mc_simple_get_host
};

/* ---- ITextProvider implementation ---- */

static HRESULT STDMETHODCALLTYPE mc_text_qi(ITextProvider *This, REFIID riid, void **ppv)
{
    mc_uia_provider *self = mc_provider_from_text(This);
    return mc_simple_qi(&self->simple_iface, riid, ppv);
}

static ULONG STDMETHODCALLTYPE mc_text_addref(ITextProvider *This)
{
    mc_uia_provider *self = mc_provider_from_text(This);
    return mc_simple_addref(&self->simple_iface);
}

static ULONG STDMETHODCALLTYPE mc_text_release(ITextProvider *This)
{
    mc_uia_provider *self = mc_provider_from_text(This);
    return mc_simple_release(&self->simple_iface);
}

static HRESULT mc_make_single_range_array(ITextRangeProvider *range, SAFEARRAY **out)
{
    SAFEARRAY *sa;
    LONG idx = 0;
    HRESULT hr;
    if (range == NULL) return E_OUTOFMEMORY;
    sa = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (sa == NULL) {
        range->lpVtbl->Release(range);
        return E_OUTOFMEMORY;
    }
    hr = SafeArrayPutElement(sa, &idx, (void *)range);
    range->lpVtbl->Release(range);
    if (FAILED(hr)) {
        SafeArrayDestroy(sa);
        return hr;
    }
    *out = sa;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mc_text_get_selection(ITextProvider *This,
                                                       SAFEARRAY **pRetVal)
{
    mc_uia_provider *self = mc_provider_from_text(This);
    monac_editor_t *ed;
    mc_selection_t pri;
    mc_range_t r;
    size_t s = 0, e = 0;

    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (self->ed == NULL) return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->ed;
    pri = mc_cursors_primary(&ed->doc.cursors);
    r = mc_selection_range(pri);
    (void)mc_pt_pos_to_offset(&ed->doc.tree, r.start, &s);
    (void)mc_pt_pos_to_offset(&ed->doc.tree, r.end, &e);
    return mc_make_single_range_array(mc_uia_range_create(self, s, e), pRetVal);
}

static HRESULT STDMETHODCALLTYPE mc_text_get_visible(ITextProvider *This,
                                                     SAFEARRAY **pRetVal)
{
    mc_uia_provider *self = mc_provider_from_text(This);
    monac_editor_t *ed;
    float lh;
    uint32_t first_v, last_v, total_v;
    mc_pos_t ps = { 0, 0 }, pe = { 0, 0 };
    size_t s = 0, e = 0;

    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (self->ed == NULL) return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->ed;
    lh = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
    total_v = mc_proj_view_line_count(&ed->doc.projection);
    if (total_v == 0) total_v = 1;
    first_v = (uint32_t)(ed->scroll_y / lh);
    last_v = (uint32_t)((ed->scroll_y + ed->viewport_h) / lh);
    if (first_v >= total_v) first_v = total_v - 1u;
    if (last_v >= total_v) last_v = total_v - 1u;

    (void)mc_proj_view_to_model(&ed->doc.projection, (mc_view_pos_t){ first_v, 0 }, &ps, NULL);
    (void)mc_proj_view_to_model(&ed->doc.projection, (mc_view_pos_t){ last_v, UINT32_MAX }, &pe, NULL);
    (void)mc_pt_pos_to_offset(&ed->doc.tree, ps, &s);
    (void)mc_pt_pos_to_offset(&ed->doc.tree, pe, &e);
    return mc_make_single_range_array(mc_uia_range_create(self, s, e), pRetVal);
}

static HRESULT STDMETHODCALLTYPE mc_text_range_from_child(
    ITextProvider *This, IRawElementProviderSimple *child,
    ITextRangeProvider **pRetVal)
{
    MC_UNUSED(This); MC_UNUSED(child);
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE mc_text_range_from_point(
    ITextProvider *This, struct UiaPoint point,
    ITextRangeProvider **pRetVal)
{
    mc_uia_provider *self = mc_provider_from_text(This);
    monac_editor_t *ed;
    POINT pt;
    float scale;
    monac_pos_t pos = { 0, 0 };
    size_t off = 0;

    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (self->ed == NULL) return UIA_E_ELEMENTNOTAVAILABLE;
    ed = self->ed;

    pt.x = (LONG)point.x;
    pt.y = (LONG)point.y;
    if (ed->hwnd != NULL) ScreenToClient(ed->hwnd, &pt);
    scale = (ed->dpi > 0.0f ? ed->dpi : 96.0f) / 96.0f;
    (void)mc_renderer_hit_test(ed, (float)pt.x / scale, (float)pt.y / scale,
                               &pos, NULL, NULL, NULL);
    (void)mc_pt_pos_to_offset(&ed->doc.tree, (mc_pos_t){ pos.line, pos.col }, &off);
    *pRetVal = mc_uia_range_create(self, off, off);
    return *pRetVal != NULL ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE mc_text_doc_range(ITextProvider *This,
                                                   ITextRangeProvider **pRetVal)
{
    mc_uia_provider *self = mc_provider_from_text(This);
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = NULL;
    if (self->ed == NULL) return UIA_E_ELEMENTNOTAVAILABLE;
    *pRetVal = mc_uia_range_create(self, 0, mc_pt_length(&self->ed->doc.tree));
    return *pRetVal != NULL ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE mc_text_supported_sel(
    ITextProvider *This, enum SupportedTextSelection *pRetVal)
{
    MC_UNUSED(This);
    if (pRetVal == NULL) return E_POINTER;
    *pRetVal = SupportedTextSelection_Single;
    return S_OK;
}

static const ITextProviderVtbl mc_uia_text_vtbl = {
    mc_text_qi,
    mc_text_addref,
    mc_text_release,
    mc_text_get_selection,
    mc_text_get_visible,
    mc_text_range_from_child,
    mc_text_range_from_point,
    mc_text_doc_range,
    mc_text_supported_sel
};

static mc_uia_provider *mc_uia_ensure(monac_editor_t *ed, HWND hwnd)
{
    mc_uia_provider *p;
    if (ed == NULL) return NULL;
    if (ed->uia != NULL) {
        ed->uia->hwnd = hwnd;
        return ed->uia;
    }
    p = (mc_uia_provider *)calloc(1, sizeof(*p));
    if (p == NULL) return NULL;
    p->simple_iface.lpVtbl = (IRawElementProviderSimpleVtbl *)&mc_uia_simple_vtbl;
    p->text_iface.lpVtbl = (ITextProviderVtbl *)&mc_uia_text_vtbl;
    p->ref_count = 1;
    p->ed = ed;
    p->hwnd = hwnd;
    ed->uia = p;
    return p;
}

void mc_uia_detach(monac_editor_t *ed)
{
    mc_uia_provider *p;
    if (ed == NULL || ed->uia == NULL) return;
    p = ed->uia;
    ed->uia = NULL;
    p->ed = NULL;
    p->hwnd = NULL;
    {
        HMODULE mod = GetModuleHandleW(L"uiautomationcore.dll");
        if (mod != NULL) {
            mc_uia_disconnect_fn fn =
                (mc_uia_disconnect_fn)(void *)GetProcAddress(mod, "UiaDisconnectProvider");
            if (fn != NULL) (void)fn(&p->simple_iface);
        }
    }
    p->simple_iface.lpVtbl->Release(&p->simple_iface);
}

LRESULT mc_uia_handle_getobject(monac_editor_t *ed, HWND hwnd,
                                WPARAM wParam, LPARAM lParam)
{
    mc_uia_provider *p = mc_uia_ensure(ed, hwnd);
    if (p == NULL) return 0;
    return UiaReturnRawElementProvider(hwnd, wParam, lParam, &p->simple_iface);
}

void mc_uia_raise_text_changed(monac_editor_t *ed)
{
    if (ed == NULL || ed->uia == NULL || !UiaClientsAreListening()) return;
    (void)UiaRaiseAutomationEvent(&ed->uia->simple_iface, UIA_Text_TextChangedEventId);
}

void mc_uia_raise_selection_changed(monac_editor_t *ed)
{
    if (ed == NULL || ed->uia == NULL || !UiaClientsAreListening()) return;
    (void)UiaRaiseAutomationEvent(&ed->uia->simple_iface, UIA_Text_TextSelectionChangedEventId);
}
