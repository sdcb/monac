#include "api/editor_internal.h"
#include "base/vec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MC_D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT ((D2D1_DRAW_TEXT_OPTIONS)0x00000004)
#ifndef D2DERR_RECREATE_TARGET
#define D2DERR_RECREATE_TARGET ((HRESULT)0x8899000CL)
#endif

static D2D1_COLOR_F mc_d2d_color(uint32_t argb)
{
    D2D1_COLOR_F c;
    c.a = (float)((argb >> 24) & 0xFFu) / 255.0f;
    c.r = (float)((argb >> 16) & 0xFFu) / 255.0f;
    c.g = (float)((argb >> 8) & 0xFFu) / 255.0f;
    c.b = (float)(argb & 0xFFu) / 255.0f;
    return c;
}

static void mc_renderer_release_brushes(mc_renderer_state *r)
{
    size_t i;
    for (i = 0; i < r->brush_count; ++i) {
        MC_SAFE_RELEASE(r->brushes[i].brush);
    }
    r->brush_count = 0;
    MC_SAFE_RELEASE(r->scratch_brush);
}

void mc_renderer_invalidate_layouts(monac_editor_t *ed)
{
    size_t i;
    if (ed == NULL) return;
    for (i = 0; i < MC_LAYOUT_CACHE_CAP; ++i) {
        MC_SAFE_RELEASE(ed->renderer.layout_cache[i].layout);
        ed->renderer.layout_cache[i].line_version = 0;
    }
}

void mc_renderer_discard_device_resources(monac_editor_t *ed)
{
    mc_renderer_state *r;
    if (ed == NULL) return;
    r = &ed->renderer;
    mc_renderer_invalidate_layouts(ed);
    mc_renderer_release_brushes(r);
    MC_SAFE_RELEASE(r->hwnd_rt);
    MC_SAFE_RELEASE(r->dc_rt);
    ++r->rt_generation;
}

static MC_ID2D1PathGeometry *mc_create_squiggle_geometry(MC_ID2D1Factory *factory)
{
    MC_ID2D1PathGeometry *geo = NULL;
    MC_ID2D1GeometrySink *sink = NULL;
    HRESULT hr;
    int i;
    const float step = 3.0f;
    const float amp = 2.0f;

    if (factory == NULL) return NULL;
    hr = MC_CALL(factory, CreatePathGeometry, &geo);
    if (FAILED(hr) || geo == NULL) return NULL;
    hr = MC_CALL(geo, Open, &sink);
    if (FAILED(hr) || sink == NULL) {
        MC_SAFE_RELEASE(geo);
        return NULL;
    }
    {
        D2D1_POINT_2F p0 = { 0.0f, amp };
        MC_CALL(sink, BeginFigure, p0, D2D1_FIGURE_BEGIN_HOLLOW);
        for (i = 1; i <= 160; ++i) {
            D2D1_POINT_2F pt;
            pt.x = (float)i * step;
            pt.y = (i & 1) ? 0.0f : amp;
            MC_CALL(sink, AddLine, pt);
        }
        MC_CALL(sink, EndFigure, D2D1_FIGURE_END_OPEN);
    }
    (void)MC_CALL0(sink, Close);
    MC_SAFE_RELEASE(sink);
    return geo;
}

int mc_renderer_init(monac_editor_t *ed)
{
    mc_renderer_state *r;
    HRESULT hr;
    if (ed == NULL) return 0;
    r = &ed->renderer;
    memset(r, 0, sizeof(*r));
    QueryPerformanceFrequency(&r->qpc_freq);

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           &IID_ID2D1Factory, NULL,
                           (void **)&r->d2d_factory);
    if (FAILED(hr) || r->d2d_factory == NULL) return 0;

    r->squiggle_geo = mc_create_squiggle_geometry(r->d2d_factory);
    r->rt_generation = 1;
    mc_proj_set_inline_draw_state(&ed->doc.projection, &r->inline_draw);
    return 1;
}

void mc_renderer_destroy(monac_editor_t *ed)
{
    mc_renderer_state *r;
    if (ed == NULL) return;
    r = &ed->renderer;
    mc_proj_set_inline_draw_state(&ed->doc.projection, NULL);
    mc_renderer_discard_device_resources(ed);
    free(r->brushes);
    r->brushes = NULL;
    r->brush_cap = 0;
    MC_SAFE_RELEASE(r->squiggle_geo);
    MC_SAFE_RELEASE(r->d2d_factory);
    memset(r, 0, sizeof(*r));
}

static MC_ID2D1SolidColorBrush *mc_get_cached_brush(mc_renderer_state *r,
                                                    MC_ID2D1RenderTarget *rt,
                                                    uint32_t argb)
{
    size_t i;
    D2D1_COLOR_F col;
    MC_ID2D1SolidColorBrush *b = NULL;
    for (i = 0; i < r->brush_count; ++i) {
        if (r->brushes[i].argb == argb) return r->brushes[i].brush;
    }
    col = mc_d2d_color(argb);
    if (FAILED(MC_CALL(rt, CreateSolidColorBrush, &col, NULL, &b)) || b == NULL)
        return r->scratch_brush;
    if (!mc_vec_reserve((void **)&r->brushes, &r->brush_cap,
                        sizeof(mc_brush_cache_entry), r->brush_count + 1)) {
        MC_SAFE_RELEASE(b);
        return r->scratch_brush;
    }
    r->brushes[r->brush_count].argb = argb;
    r->brushes[r->brush_count].brush = b;
    ++r->brush_count;
    return b;
}

static void mc_probe_color_font(mc_renderer_state *r, MC_ID2D1RenderTarget *rt)
{
    MC_IUnknown *dc = NULL;
    if (rt != NULL && SUCCEEDED(MC_QUERY(rt, &IID_ID2D1DeviceContext, &dc)) && dc != NULL) {
        r->supports_color_font = true;
        MC_SAFE_RELEASE(dc);
    } else {
        r->supports_color_font = false;
    }
}

static MC_ID2D1RenderTarget *mc_ensure_hwnd_rt(monac_editor_t *ed)
{
    mc_renderer_state *r = &ed->renderer;
    RECT rc;
    D2D1_RENDER_TARGET_PROPERTIES rt_props;
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props;
    HRESULT hr;

    if (ed->hwnd == NULL || r->d2d_factory == NULL) return NULL;
    if (r->using_dc_rt) {
        mc_renderer_discard_device_resources(ed);
        r->using_dc_rt = false;
    }
    if (r->hwnd_rt != NULL) return MC_AS(ID2D1RenderTarget, r->hwnd_rt);

    GetClientRect(ed->hwnd, &rc);
    if (rc.right <= rc.left) rc.right = rc.left + 1;
    if (rc.bottom <= rc.top) rc.bottom = rc.top + 1;

    memset(&rt_props, 0, sizeof(rt_props));
    rt_props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rt_props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rt_props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    rt_props.dpiX = ed->dpi > 0.0f ? ed->dpi : 96.0f;
    rt_props.dpiY = ed->dpi > 0.0f ? ed->dpi : 96.0f;

    memset(&hwnd_props, 0, sizeof(hwnd_props));
    hwnd_props.hwnd = ed->hwnd;
    hwnd_props.pixelSize.width = (UINT32)(rc.right - rc.left);
    hwnd_props.pixelSize.height = (UINT32)(rc.bottom - rc.top);
    hwnd_props.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

    hr = MC_CALL(r->d2d_factory, CreateHwndRenderTarget,
                 &rt_props, &hwnd_props, &r->hwnd_rt);
    if (FAILED(hr) || r->hwnd_rt == NULL) return NULL;

    {
        MC_ID2D1RenderTarget *rt = MC_AS(ID2D1RenderTarget, r->hwnd_rt);
        D2D1_COLOR_F white = { 1.0f, 1.0f, 1.0f, 1.0f };
        (void)MC_CALL(rt, CreateSolidColorBrush, &white, NULL, &r->scratch_brush);
        mc_probe_color_font(r, rt);
        ++r->rt_generation;
        return rt;
    }
}

void mc_renderer_resize(monac_editor_t *ed, uint32_t w, uint32_t h)
{
    if (ed == NULL) return;
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (ed->renderer.hwnd_rt != NULL) {
        D2D1_SIZE_U sz;
        sz.width = w;
        sz.height = h;
        (void)MC_CALL(ed->renderer.hwnd_rt, Resize, &sz);
    }
}

static void mc_apply_style_to_span(monac_editor_t *ed,
                                   MC_ID2D1RenderTarget *rt,
                                   MC_IDWriteTextLayout *layout,
                                   const mc_logical_line_t *ll,
                                   const mc_wrap_span_t *span)
{
    size_t s;
    uint32_t span_end = span->start + span->length;

    /* 1. Inline objects inside this span */
    for (s = 0; s < ll->inline_count; ++s) {
        uint32_t pos = ll->inlines[s].pos;
        if (ll->inlines[s].object != NULL && pos >= span->start && pos < span_end) {
            DWRITE_TEXT_RANGE r = { pos - span->start, 1 };
            (void)MC_CALL(layout, SetInlineObject, ll->inlines[s].object, r);
        }
    }

    if (rt == NULL) return;

    /* 2. Syntax highlighting tokens mapped through MC_SEG_TEXT segments */
    for (s = 0; s < ll->seg_count; ++s) {
        const mc_proj_segment_t *seg = &ll->segments[s];
        const mc_line_tokens_t *lt;
        size_t t;
        if (seg->kind != MC_SEG_TEXT || seg->view_col_len == 0) continue;
        if (seg->view_col_start + seg->view_col_len <= span->start ||
            seg->view_col_start >= span_end) {
            continue;
        }
        (void)mc_lexer_cache_ensure_line(&ed->lexer, &ed->doc.tree, seg->model_line);
        lt = mc_lexer_cache_get_line(&ed->lexer, seg->model_line);
        if (lt != NULL) {
            for (t = 0; t < lt->count; ++t) {
                uint32_t tok_s = lt->tokens[t].start_col;
                uint32_t tok_e = tok_s + lt->tokens[t].length;
                uint32_t seg_ms = seg->model_col_start;
                uint32_t seg_me = seg_ms + seg->model_col_len;
                uint32_t ov_s = tok_s > seg_ms ? tok_s : seg_ms;
                uint32_t ov_e = tok_e < seg_me ? tok_e : seg_me;
                if (ov_s < ov_e) {
                    uint32_t v_s = seg->view_col_start + (ov_s - seg_ms);
                    uint32_t v_e = seg->view_col_start + (ov_e - seg_ms);
                    uint32_t cl_s = v_s > span->start ? v_s : span->start;
                    uint32_t cl_e = v_e < span_end ? v_e : span_end;
                    if (cl_s < cl_e) {
                        uint32_t sid = lt->tokens[t].style_id;
                        if (sid < MC_MAX_TOKEN_STYLES && ed->token_styles[sid].fg_color != 0) {
                            MC_ID2D1SolidColorBrush *br =
                                mc_get_cached_brush(&ed->renderer, rt,
                                                    ed->token_styles[sid].fg_color);
                            DWRITE_TEXT_RANGE tr = { cl_s - span->start, cl_e - cl_s };
                            if (br != NULL) {
                                (void)MC_CALL(layout, SetDrawingEffect,
                                              MC_AS(IUnknown, br), tr);
                            }
                            if (ed->token_styles[sid].bold) {
                                (void)MC_CALL(layout, SetFontWeight,
                                              DWRITE_FONT_WEIGHT_SEMI_BOLD, tr);
                            }
                            if (ed->token_styles[sid].italic) {
                                (void)MC_CALL(layout, SetFontStyle,
                                              DWRITE_FONT_STYLE_ITALIC, tr);
                            }
                        }
                    }
                }
            }
        }

        /* 3. Semantic tokens overlay */
        for (t = 0; t < ed->semantic_count; ++t) {
            const monac_semantic_token_t *st = &ed->semantic_tokens[t];
            uint32_t seg_ms = seg->model_col_start;
            uint32_t seg_me = seg_ms + seg->model_col_len;
            uint32_t ov_s, ov_e;
            if (st->line != seg->model_line) continue;
            ov_s = st->col > seg_ms ? st->col : seg_ms;
            ov_e = (st->col + st->len) < seg_me ? (st->col + st->len) : seg_me;
            if (ov_s < ov_e && st->style_id < MC_MAX_TOKEN_STYLES &&
                ed->token_styles[st->style_id].fg_color != 0) {
                uint32_t v_s = seg->view_col_start + (ov_s - seg_ms);
                uint32_t v_e = seg->view_col_start + (ov_e - seg_ms);
                uint32_t cl_s = v_s > span->start ? v_s : span->start;
                uint32_t cl_e = v_e < span_end ? v_e : span_end;
                if (cl_s < cl_e) {
                    MC_ID2D1SolidColorBrush *br =
                        mc_get_cached_brush(&ed->renderer, rt,
                                            ed->token_styles[st->style_id].fg_color);
                    DWRITE_TEXT_RANGE tr = { cl_s - span->start, cl_e - cl_s };
                    if (br != NULL) {
                        (void)MC_CALL(layout, SetDrawingEffect,
                                      MC_AS(IUnknown, br), tr);
                    }
                }
            }
        }
    }
}

static MC_IDWriteTextLayout *mc_get_span_layout(monac_editor_t *ed,
                                                MC_ID2D1RenderTarget *rt,
                                                const mc_logical_line_t *ll,
                                                const mc_wrap_span_t *span)
{
    mc_renderer_state *r = &ed->renderer;
    size_t i;
    size_t victim = 0;
    uint64_t oldest = UINT64_MAX;
    uint32_t rt_gen = (rt != NULL) ? r->rt_generation : 0u;
    MC_IDWriteTextLayout *layout;

    ++r->cache_tick;
    for (i = 0; i < MC_LAYOUT_CACHE_CAP; ++i) {
        mc_layout_cache_entry *e = &r->layout_cache[i];
        if (e->layout != NULL &&
            e->first_model_line == ll->first_model_line &&
            e->line_version == ll->version &&
            e->span_start == span->start &&
            e->span_len == span->length &&
            e->font_version == ed->doc.dw.font_version &&
            e->syntax_version == ed->syntax_version &&
            e->rt_generation == rt_gen) {
            e->last_used = r->cache_tick;
            return e->layout;
        }
        if (e->layout == NULL) {
            victim = i;
            oldest = 0;
        } else if (e->last_used < oldest) {
            oldest = e->last_used;
            victim = i;
        }
    }

    {
        const wchar_t *text = (ll->text.data != NULL && span->start <= ll->text.length)
                                  ? (ll->text.data + span->start)
                                  : L"";
        uint32_t len = (span->start + span->length <= ll->text.length)
                           ? span->length
                           : (span->start < ll->text.length
                                  ? (uint32_t)(ll->text.length - span->start)
                                  : 0u);
        /* For very long unwrapped lines, cap single layout length to avoid DWrite limits */
        if (len > 16384u) len = 16384u;
        layout = mc_dw_create_layout(&ed->doc.dw, text, len, 1000000.0f, false);
        if (layout == NULL) return NULL;
        mc_apply_style_to_span(ed, rt, layout, ll, span);
    }

    MC_SAFE_RELEASE(r->layout_cache[victim].layout);
    r->layout_cache[victim].first_model_line = ll->first_model_line;
    r->layout_cache[victim].line_version = ll->version;
    r->layout_cache[victim].span_start = span->start;
    r->layout_cache[victim].span_len = span->length;
    r->layout_cache[victim].font_version = ed->doc.dw.font_version;
    r->layout_cache[victim].syntax_version = ed->syntax_version;
    r->layout_cache[victim].rt_generation = rt_gen;
    r->layout_cache[victim].last_used = r->cache_tick;
    r->layout_cache[victim].layout = layout;
    return layout;
}

static float mc_view_col_to_x(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                              const mc_logical_line_t *ll,
                              const mc_wrap_span_t *span,
                              uint32_t view_col)
{
    MC_IDWriteTextLayout *layout = mc_get_span_layout(ed, rt, ll, span);
    FLOAT px = 0.0f, py = 0.0f;
    DWRITE_HIT_TEST_METRICS htm;
    if (layout == NULL) return span->indent_x + (float)view_col * ed->doc.dw.space_width;
    if (view_col > span->length) view_col = span->length;
    memset(&htm, 0, sizeof(htm));
    if (SUCCEEDED(MC_CALL(layout, HitTestTextPosition, view_col, FALSE, &px, &py, &htm))) {
        return span->indent_x + px;
    }
    return span->indent_x + (float)view_col * ed->doc.dw.space_width;
}

static void mc_draw_squiggle(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                             MC_ID2D1SolidColorBrush *brush,
                             float x1, float x2, float y, uint32_t argb)
{
    D2D1_COLOR_F col;
    D2D1_RECT_F clip;
    D2D1_MATRIX_3X2_F old_xf, xf;
    if (x2 <= x1 + 1.0f || ed->renderer.squiggle_geo == NULL) return;
    col = mc_d2d_color(argb);
    MC_CALL(brush, SetColor, &col);
    clip.left = x1;
    clip.top = y - 2.0f;
    clip.right = x2;
    clip.bottom = y + 4.0f;
    MC_CALL(rt, PushAxisAlignedClip, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    MC_CALL(rt, GetTransform, &old_xf);
    xf._11 = 1.0f; xf._12 = 0.0f;
    xf._21 = 0.0f; xf._22 = 1.0f;
    xf._31 = x1;   xf._32 = y;
    MC_CALL(rt, SetTransform, &xf);
    MC_CALL(rt, DrawGeometry, MC_AS(ID2D1Geometry, ed->renderer.squiggle_geo),
            MC_AS(ID2D1Brush, brush), 1.15f, NULL);
    MC_CALL(rt, SetTransform, &old_xf);
    MC_CALL0(rt, PopAxisAlignedClip);
}

static void mc_record_frame_time(mc_renderer_state *r, double ms)
{
    r->frame_times_ms[r->frame_head] = ms;
    r->frame_head = (r->frame_head + 1u) % MC_FRAME_STATS_CAP;
    if (r->frame_count < MC_FRAME_STATS_CAP) ++r->frame_count;
}

static HRESULT mc_renderer_draw_frame(monac_editor_t *ed,
                                      MC_ID2D1RenderTarget *rt,
                                      float width, float height)
{
    LARGE_INTEGER t0, t1;
    mc_renderer_state *r = &ed->renderer;
    MC_ID2D1SolidColorBrush *brush = r->scratch_brush;
    D2D1_COLOR_F col;
    float line_h = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
    float gutter_w = mc_editor_gutter_width(ed);
    float text_x0 = mc_editor_text_left(ed);
    float sb_w = mc_editor_scrollbar_width();
    uint32_t total_vlines;
    uint32_t first_v, last_v, v;
    mc_view_pos_t primary_vp = { 0, 0 };
    mc_fold_info_t *all_folds = NULL;
    size_t fold_count = 0;
    D2D1_RECT_F text_clip;
    D2D1_DRAW_TEXT_OPTIONS text_opts;

    if (rt == NULL || brush == NULL) return E_FAIL;

    QueryPerformanceCounter(&t0);

    (void)mc_proj_ensure_valid(&ed->doc.projection);
    total_vlines = mc_proj_view_line_count(&ed->doc.projection);
    if (total_vlines == 0) total_vlines = 1;

    /* Clamp scroll_y */
    {
        float content_h = (float)total_vlines * line_h + line_h * 2.0f;
        float max_sy = content_h > height ? (content_h - height) : 0.0f;
        if (ed->scroll_y < 0.0f) ed->scroll_y = 0.0f;
        if (ed->scroll_y > max_sy) ed->scroll_y = max_sy;
        if (ed->doc.projection.wrap_mode == MC_WRAP_WORD || ed->scroll_x < 0.0f)
            ed->scroll_x = 0.0f;
    }

    first_v = (uint32_t)(ed->scroll_y / line_h);
    last_v = (uint32_t)((ed->scroll_y + height) / line_h) + 1u;
    if (last_v >= total_vlines) last_v = total_vlines - 1u;

    {
        mc_selection_t pri = mc_cursors_primary(&ed->doc.cursors);
        (void)mc_proj_model_to_view(&ed->doc.projection, pri.active, &primary_vp);
    }

    fold_count = mc_folding_count(&ed->doc.folding);
    if (fold_count > 0) {
        all_folds = (mc_fold_info_t *)malloc(fold_count * sizeof(mc_fold_info_t));
        if (all_folds != NULL) {
            fold_count = mc_folding_list(&ed->doc.folding, &ed->doc.tree,
                                         all_folds, fold_count);
        } else {
            fold_count = 0;
        }
    }

    text_opts = r->supports_color_font
                    ? MC_D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
                    : D2D1_DRAW_TEXT_OPTIONS_NONE;

    MC_CALL0(rt, BeginDraw);

    /* 1. Background & Current Line Highlight */
    col = mc_d2d_color(ed->theme.bg);
    MC_CALL(rt, Clear, &col);

    if (primary_vp.view_line >= first_v && primary_vp.view_line <= last_v) {
        D2D1_RECT_F cur_rc;
        float y = (float)primary_vp.view_line * line_h - ed->scroll_y;
        cur_rc.left = gutter_w;
        cur_rc.top = y;
        cur_rc.right = width;
        cur_rc.bottom = y + line_h;
        col = mc_d2d_color(ed->theme.current_line_bg);
        MC_CALL(brush, SetColor, &col);
        MC_CALL(rt, FillRectangle, &cur_rc, MC_AS(ID2D1Brush, brush));
    }

    /* Clip text region (layers 2, 3, 5, 6, 7) */
    text_clip.left = gutter_w;
    text_clip.top = 0.0f;
    text_clip.right = width - sb_w;
    text_clip.bottom = height;
    MC_CALL(rt, PushAxisAlignedClip, &text_clip, D2D1_ANTIALIAS_MODE_ALIASED);

    /* 2. Indent Guides & 3. Selections & Decorations BG */
    for (v = first_v; v <= last_v; ++v) {
        uint32_t sub = 0;
        mc_wrap_span_t span;
        const mc_logical_line_t *ll = mc_proj_get_view_line(&ed->doc.projection, v, &sub, &span);
        float y = (float)v * line_h - ed->scroll_y;
        if (ll == NULL) continue;

        /* Indent guides on sub == 0 */
        if (ed->show_indent_guides && sub == 0 && ll->text.length > 0) {
            uint32_t spaces = 0;
            uint32_t i;
            uint32_t tab_sz = ed->doc.tab_size > 0 ? ed->doc.tab_size : 4;
            for (i = 0; i < ll->text.length; ++i) {
                if (ll->text.data[i] == L' ') ++spaces;
                else if (ll->text.data[i] == L'\t') spaces += tab_sz;
                else break;
            }
            if (spaces >= tab_sz) {
                uint32_t g;
                col = mc_d2d_color(ed->theme.indent_guide);
                MC_CALL(brush, SetColor, &col);
                for (g = tab_sz; g < spaces; g += tab_sz) {
                    float gx = text_x0 - ed->scroll_x + (float)g * ed->doc.dw.space_width + 0.5f;
                    D2D1_POINT_2F p0 = { gx, y };
                    D2D1_POINT_2F p1 = { gx, y + line_h };
                    MC_CALL(rt, DrawLine, p0, p1, MC_AS(ID2D1Brush, brush), 1.0f, NULL);
                }
            }
        }

        /* Decorations background/border for segments visible on this view line */
        {
            size_t line_off_start = mc_pt_line_start_offset(&ed->doc.tree, ll->first_model_line);
            size_t line_off_end = mc_pt_line_start_offset(&ed->doc.tree, ll->last_model_line) +
                                  mc_pt_line_length(&ed->doc.tree, ll->last_model_line);
            mc_dec_snapshot_t decs[64];
            size_t dcnt = mc_dec_query(&ed->doc.decorations, line_off_start, line_off_end, decs, 64);
            size_t d;
            for (d = 0; d < dcnt && d < 64; ++d) {
                mc_pos_t ds, de;
                mc_view_pos_t vs, ve;
                if (decs[d].owner_id == MC_OWNER_FOLDING) continue;
                if ((decs[d].style.bg_color >> 24) == 0 &&
                    (decs[d].style.border_color >> 24) == 0) continue;
                if (!mc_pt_offset_to_pos(&ed->doc.tree, decs[d].start, &ds)) continue;
                if (!mc_pt_offset_to_pos(&ed->doc.tree, decs[d].end, &de)) continue;
                if (!mc_proj_model_to_view(&ed->doc.projection, ds, &vs)) continue;
                if (!mc_proj_model_to_view(&ed->doc.projection, de, &ve)) continue;
                if (v < vs.view_line || v > ve.view_line) continue;
                {
                    uint32_t c0 = (v == vs.view_line) ? vs.view_col : 0;
                    uint32_t c1 = (v == ve.view_line) ? ve.view_col : span.length;
                    float x0 = text_x0 - ed->scroll_x + mc_view_col_to_x(ed, rt, ll, &span, c0);
                    float x1 = text_x0 - ed->scroll_x + mc_view_col_to_x(ed, rt, ll, &span, c1);
                    if (x1 > x0) {
                        D2D1_RECT_F drc = { x0, y + 1.0f, x1, y + line_h - 1.0f };
                        if ((decs[d].style.bg_color >> 24) != 0) {
                            col = mc_d2d_color(decs[d].style.bg_color);
                            MC_CALL(brush, SetColor, &col);
                            MC_CALL(rt, FillRectangle, &drc, MC_AS(ID2D1Brush, brush));
                        }
                        if ((decs[d].style.border_color >> 24) != 0) {
                            col = mc_d2d_color(decs[d].style.border_color);
                            MC_CALL(brush, SetColor, &col);
                            MC_CALL(rt, DrawRectangle, &drc, MC_AS(ID2D1Brush, brush), 1.0f, NULL);
                        }
                    }
                }
            }
        }

        /* Selections */
        {
            size_t si;
            col = mc_d2d_color(ed->theme.selection_bg);
            MC_CALL(brush, SetColor, &col);
            for (si = 0; si < ed->doc.cursors.count; ++si) {
                mc_range_t sr = mc_selection_range(ed->doc.cursors.items[si]);
                mc_view_pos_t vs, ve;
                if (mc_range_is_empty(sr)) continue;
                if (!mc_proj_model_to_view(&ed->doc.projection, sr.start, &vs)) continue;
                if (!mc_proj_model_to_view(&ed->doc.projection, sr.end, &ve)) continue;
                if (v < vs.view_line || v > ve.view_line) continue;
                {
                    uint32_t c0 = (v == vs.view_line) ? vs.view_col : 0;
                    uint32_t c1 = (v == ve.view_line) ? ve.view_col : span.length;
                    float x0 = text_x0 - ed->scroll_x + mc_view_col_to_x(ed, rt, ll, &span, c0);
                    float x1 = text_x0 - ed->scroll_x + mc_view_col_to_x(ed, rt, ll, &span, c1);
                    if (v < ve.view_line) x1 += ed->doc.dw.space_width * 0.6f;
                    if (x1 > x0) {
                        D2D1_RECT_F src_rc = { x0, y, x1, y + line_h };
                        MC_CALL(rt, FillRectangle, &src_rc, MC_AS(ID2D1Brush, brush));
                    }
                }
            }
        }
    }

    /* 5. Body Text + Inline Fold Badges & Inlay Capsules */
    r->inline_draw.rt = rt;
    r->inline_draw.brush = brush;
    r->inline_draw.hovered_fold_id = ed->hovered_fold_id;
    r->inline_draw.origin_offset_x = 0.0f;
    r->inline_draw.origin_offset_y = 0.0f;

    {
        MC_ID2D1SolidColorBrush *fg_brush = mc_get_cached_brush(r, rt, ed->theme.fg);
        for (v = first_v; v <= last_v; ++v) {
            uint32_t sub = 0;
            mc_wrap_span_t span;
            const mc_logical_line_t *ll = mc_proj_get_view_line(&ed->doc.projection, v, &sub, &span);
            float y = (float)v * line_h - ed->scroll_y;
            MC_IDWriteTextLayout *layout;
            D2D1_POINT_2F origin;

            if (ll == NULL) continue;
            layout = mc_get_span_layout(ed, rt, ll, &span);
            if (layout == NULL) continue;

            origin.x = text_x0 - ed->scroll_x + span.indent_x;
            origin.y = y;
            r->inline_draw.origin_offset_x = origin.x;
            r->inline_draw.origin_offset_y = origin.y;
            MC_CALL(rt, DrawTextLayout, origin, layout,
                    MC_AS(ID2D1Brush, fg_brush ? fg_brush : brush), text_opts);
        }
    }
    r->inline_draw.rt = NULL;
    r->inline_draw.brush = NULL;

    /* 6. Diagnostic Squiggly & Solid Underlines */
    for (v = first_v; v <= last_v; ++v) {
        uint32_t sub = 0;
        mc_wrap_span_t span;
        const mc_logical_line_t *ll = mc_proj_get_view_line(&ed->doc.projection, v, &sub, &span);
        float y = (float)v * line_h - ed->scroll_y;
        size_t line_off_start, line_off_end;
        mc_dec_snapshot_t decs[64];
        size_t dcnt, d;
        if (ll == NULL) continue;
        line_off_start = mc_pt_line_start_offset(&ed->doc.tree, ll->first_model_line);
        line_off_end = mc_pt_line_start_offset(&ed->doc.tree, ll->last_model_line) +
                       mc_pt_line_length(&ed->doc.tree, ll->last_model_line);
        dcnt = mc_dec_query(&ed->doc.decorations, line_off_start, line_off_end, decs, 64);
        for (d = 0; d < dcnt && d < 64; ++d) {
            mc_pos_t ds, de;
            mc_view_pos_t vs, ve;
            uint32_t ucol;
            if (decs[d].style.underline_style == MC_UNDERLINE_NONE) continue;
            if (!mc_pt_offset_to_pos(&ed->doc.tree, decs[d].start, &ds)) continue;
            if (!mc_pt_offset_to_pos(&ed->doc.tree, decs[d].end, &de)) continue;
            if (!mc_proj_model_to_view(&ed->doc.projection, ds, &vs)) continue;
            if (!mc_proj_model_to_view(&ed->doc.projection, de, &ve)) continue;
            if (v < vs.view_line || v > ve.view_line) continue;
            ucol = decs[d].style.underline_color;
            if (ucol == 0) {
                if (decs[d].style.underline_style == MC_UNDERLINE_SQUIGGLY_ERROR) ucol = 0xFFF14C4Cu;
                else if (decs[d].style.underline_style == MC_UNDERLINE_SQUIGGLY_WARNING) ucol = 0xFFCCA700u;
                else ucol = 0xFF3794FFu;
            }
            {
                uint32_t c0 = (v == vs.view_line) ? vs.view_col : 0;
                uint32_t c1 = (v == ve.view_line) ? ve.view_col : span.length;
                float x0 = text_x0 - ed->scroll_x + mc_view_col_to_x(ed, rt, ll, &span, c0);
                float x1 = text_x0 - ed->scroll_x + mc_view_col_to_x(ed, rt, ll, &span, c1);
                if (x1 <= x0 + 2.0f) x1 = x0 + ed->doc.dw.space_width;
                if (decs[d].style.underline_style == MC_UNDERLINE_SOLID) {
                    D2D1_POINT_2F p0 = { x0, y + line_h - 2.0f };
                    D2D1_POINT_2F p1 = { x1, y + line_h - 2.0f };
                    col = mc_d2d_color(ucol);
                    MC_CALL(brush, SetColor, &col);
                    MC_CALL(rt, DrawLine, p0, p1, MC_AS(ID2D1Brush, brush), 1.0f, NULL);
                } else {
                    mc_draw_squiggle(ed, rt, brush, x0, x1, y + line_h - 3.5f, ucol);
                }
            }
        }
    }

    /* 7. Blinking Multi-Carets & Inline IME Composition */
    if (ed->caret_visible || !ed->has_focus) {
        size_t si;
        col = mc_d2d_color(ed->theme.caret);
        MC_CALL(brush, SetColor, &col);
        for (si = 0; si < ed->doc.cursors.count; ++si) {
            mc_view_pos_t vp;
            uint32_t sub = 0;
            mc_wrap_span_t span;
            const mc_logical_line_t *ll;
            float cx, cy;
            if (!mc_proj_model_to_view(&ed->doc.projection, ed->doc.cursors.items[si].active, &vp))
                continue;
            if (vp.view_line < first_v || vp.view_line > last_v) continue;
            ll = mc_proj_get_view_line(&ed->doc.projection, vp.view_line, &sub, &span);
            if (ll == NULL) continue;
            cx = text_x0 - ed->scroll_x + mc_view_col_to_x(ed, rt, ll, &span, vp.view_col);
            cy = (float)vp.view_line * line_h - ed->scroll_y;
            if (ed->has_focus && ed->caret_visible && !mc_overlays_has_text_focus(ed)) {
                D2D1_RECT_F crc = { cx, cy + 1.0f, cx + 1.8f, cy + line_h - 1.0f };
                MC_CALL(rt, FillRectangle, &crc, MC_AS(ID2D1Brush, brush));
            }
            if (si == ed->doc.cursors.primary_index && ed->ime.active && ed->ime.comp_str.length > 0 &&
                !mc_overlays_has_text_focus(ed)) {
                MC_IDWriteTextLayout *cl = mc_dw_create_layout(
                    &ed->doc.dw, ed->ime.comp_str.data, (uint32_t)ed->ime.comp_str.length,
                    10000.0f, false);
                if (cl != NULL) {
                    DWRITE_TEXT_METRICS tm;
                    memset(&tm, 0, sizeof(tm));
                    (void)MC_CALL(cl, GetMetrics, &tm);
                    {
                        D2D1_RECT_F bg = { cx, cy, cx + tm.widthIncludingTrailingWhitespace + 4.0f, cy + line_h };
                        D2D1_POINT_2F pt = { cx + 2.0f, cy };
                        D2D1_POINT_2F u0 = { cx, cy + line_h - 1.5f };
                        D2D1_POINT_2F u1 = { bg.right, cy + line_h - 1.5f };
                        col = mc_d2d_color(0xFF2B3242u);
                        MC_CALL(brush, SetColor, &col);
                        MC_CALL(rt, FillRectangle, &bg, MC_AS(ID2D1Brush, brush));
                        col = mc_d2d_color(0xFFFFFFFFu);
                        MC_CALL(brush, SetColor, &col);
                        MC_CALL(rt, DrawTextLayout, pt, cl, MC_AS(ID2D1Brush, brush), text_opts);
                        col = mc_d2d_color(0xFF569CD6u);
                        MC_CALL(brush, SetColor, &col);
                        MC_CALL(rt, DrawLine, u0, u1, MC_AS(ID2D1Brush, brush), 1.5f, NULL);
                    }
                    MC_SAFE_RELEASE(cl);
                }
            }
        }
    }

    MC_CALL0(rt, PopAxisAlignedClip);

    /* 4. Left Gutter (Line Numbers, Multiline Fold Chevrons, Diagnostic Dots) */
    if (gutter_w > 0.0f) {
        D2D1_RECT_F grc = { 0.0f, 0.0f, gutter_w, height };
        col = mc_d2d_color(ed->theme.gutter_bg);
        MC_CALL(brush, SetColor, &col);
        MC_CALL(rt, FillRectangle, &grc, MC_AS(ID2D1Brush, brush));

        {
            D2D1_POINT_2F p0 = { gutter_w - 0.5f, 0.0f };
            D2D1_POINT_2F p1 = { gutter_w - 0.5f, height };
            col = mc_d2d_color(0xFF2B2F3Au);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, DrawLine, p0, p1, MC_AS(ID2D1Brush, brush), 1.0f, NULL);
        }

        for (v = first_v; v <= last_v; ++v) {
            uint32_t sub = 0;
            mc_wrap_span_t span;
            const mc_logical_line_t *ll = mc_proj_get_view_line(&ed->doc.projection, v, &sub, &span);
            float y = (float)v * line_h - ed->scroll_y;
            if (ll == NULL || sub != 0) continue;

            /* Diagnostic gutter dot */
            {
                size_t ls = mc_pt_line_start_offset(&ed->doc.tree, ll->first_model_line);
                size_t le = ls + mc_pt_line_length(&ed->doc.tree, ll->first_model_line);
                mc_dec_snapshot_t decs[16];
                size_t dc = mc_dec_query(&ed->doc.decorations, ls, le, decs, 16);
                size_t di;
                for (di = 0; di < dc && di < 16; ++di) {
                    if (decs[di].style.gutter_icon != 0) {
                        uint32_t gcol = decs[di].style.gutter_color != 0
                                            ? decs[di].style.gutter_color
                                            : 0xFFF14C4Cu;
                        D2D1_ELLIPSE el;
                        el.point.x = 7.0f;
                        el.point.y = y + line_h * 0.5f;
                        el.radiusX = 3.5f;
                        el.radiusY = 3.5f;
                        col = mc_d2d_color(gcol);
                        MC_CALL(brush, SetColor, &col);
                        MC_CALL(rt, FillEllipse, &el, MC_AS(ID2D1Brush, brush));
                        break;
                    }
                }
            }

            /* Line number */
            if (ed->show_line_numbers) {
                wchar_t num_buf[24];
                int nlen = swprintf(num_buf, 24, L"%u", ll->first_model_line + 1u);
                if (nlen > 0) {
                    D2D1_RECT_F nrc = { 12.0f, y, gutter_w - 20.0f, y + line_h };
                    uint32_t ncolor = (v == primary_vp.view_line)
                                          ? ed->theme.gutter_active_fg
                                          : ed->theme.gutter_fg;
                    col = mc_d2d_color(ncolor);
                    MC_CALL(brush, SetColor, &col);
                    (void)MC_CALL(ed->doc.dw.text_format, SetTextAlignment,
                                  DWRITE_TEXT_ALIGNMENT_TRAILING);
                    MC_CALL(rt, DrawText, num_buf, (UINT32)nlen,
                            ed->doc.dw.text_format, &nrc,
                            MC_AS(ID2D1Brush, brush), D2D1_DRAW_TEXT_OPTIONS_NONE,
                            DWRITE_MEASURING_MODE_NATURAL);
                    (void)MC_CALL(ed->doc.dw.text_format, SetTextAlignment,
                                  DWRITE_TEXT_ALIGNMENT_LEADING);
                }
            }

            /* Multiline fold chevron */
            {
                size_t fi;
                for (fi = 0; fi < fold_count; ++fi) {
                    if (all_folds[fi].range.start.line == ll->first_model_line &&
                        all_folds[fi].range.end.line > all_folds[fi].range.start.line) {
                        const wchar_t *glyph = all_folds[fi].collapsed ? L"\x25B8" : L"\x25BE";
                        D2D1_RECT_F crc = { gutter_w - 17.0f, y, gutter_w - 2.0f, y + line_h };
                        col = mc_d2d_color(all_folds[fi].collapsed ? 0xFFE5C07Bu : 0xFF8A919Eu);
                        MC_CALL(brush, SetColor, &col);
                        MC_CALL(rt, DrawText, glyph, 1, ed->doc.dw.text_format, &crc,
                                MC_AS(ID2D1Brush, brush), D2D1_DRAW_TEXT_OPTIONS_NONE,
                                DWRITE_MEASURING_MODE_NATURAL);
                        break;
                    }
                }
            }
        }
    }

    /* 8. Lightweight Custom Scrollbars */
    {
        float content_h = (float)total_vlines * line_h + line_h * 2.0f;
        if (content_h > height && height > 24.0f) {
            float thumb_h = (height / content_h) * height;
            float track_h = height - thumb_h;
            float ratio = ed->scroll_y / (content_h - height);
            float ty;
            D2D1_ROUNDED_RECT rr;
            if (thumb_h < 24.0f) {
                thumb_h = 24.0f;
                track_h = height - thumb_h;
            }
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            ty = ratio * track_h;
            rr.rect.left = width - sb_w + 2.0f;
            rr.rect.top = ty + 2.0f;
            rr.rect.right = width - 2.0f;
            rr.rect.bottom = ty + thumb_h - 2.0f;
            rr.radiusX = 3.0f;
            rr.radiusY = 3.0f;
            col = mc_d2d_color(ed->theme.scrollbar_thumb);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, FillRoundedRectangle, &rr, MC_AS(ID2D1Brush, brush));
        }
        if (ed->doc.projection.wrap_mode == MC_WRAP_OFF && ed->scroll_x > 0.0f && width > gutter_w + 40.0f) {
            D2D1_ROUNDED_RECT hrr;
            hrr.rect.left = gutter_w + 4.0f;
            hrr.rect.top = height - 8.0f;
            hrr.rect.right = gutter_w + 80.0f;
            hrr.rect.bottom = height - 2.0f;
            hrr.radiusX = 3.0f;
            hrr.radiusY = 3.0f;
            col = mc_d2d_color(ed->theme.scrollbar_thumb);
            MC_CALL(brush, SetColor, &col);
            MC_CALL(rt, FillRoundedRectangle, &hrr, MC_AS(ID2D1Brush, brush));
        }
    }

    /* 9. Built-in D2D Overlays (Find Bar -> Suggest Popup -> Hover Popup) */
    mc_overlays_draw(ed, rt, brush);

    {
        HRESULT hr = MC_CALL(rt, EndDraw, NULL, NULL);
        QueryPerformanceCounter(&t1);
        if (r->qpc_freq.QuadPart > 0) {
            double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 /
                        (double)r->qpc_freq.QuadPart;
            mc_record_frame_time(r, ms);
        }
        free(all_folds);
        return hr;
    }
}

void mc_renderer_paint_hwnd(monac_editor_t *ed)
{
    MC_ID2D1RenderTarget *rt;
    HRESULT hr;
    if (ed == NULL || ed->hwnd == NULL) return;
    rt = mc_ensure_hwnd_rt(ed);
    if (rt == NULL) return;

    {
        D2D1_SIZE_F sz = { 0.0f, 0.0f };
        (void)MC_CALL(rt, GetSize, &sz);
        hr = mc_renderer_draw_frame(ed, rt, sz.width, sz.height);
    }
    if (hr == D2DERR_RECREATE_TARGET) {
        mc_renderer_discard_device_resources(ed);
        InvalidateRect(ed->hwnd, NULL, FALSE);
    }
}

bool mc_renderer_render_to_bgra(monac_editor_t *ed, uint32_t width, uint32_t height,
                                void *pixels, uint32_t stride)
{
    mc_renderer_state *r;
    HDC mem_dc = NULL;
    HBITMAP dib = NULL;
    HGDIOBJ old_bmp = NULL;
    void *dib_bits = NULL;
    BITMAPINFO bmi;
    RECT rc;
    HRESULT hr;
    float saved_w, saved_h;
    bool ok = false;

    if (ed == NULL || pixels == NULL || width == 0 || height == 0 ||
        stride < width * 4u) return false;
    r = &ed->renderer;
    if (r->d2d_factory == NULL) return false;

    /* Switch to DC render target cleanly */
    if (!r->using_dc_rt || r->dc_rt == NULL) {
        mc_renderer_discard_device_resources(ed);
        r->using_dc_rt = true;
        {
            D2D1_RENDER_TARGET_PROPERTIES props;
            memset(&props, 0, sizeof(props));
            props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
            props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            props.dpiX = 96.0f;
            props.dpiY = 96.0f;
            props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
            hr = MC_CALL(r->d2d_factory, CreateDCRenderTarget, &props, &r->dc_rt);
            if (FAILED(hr) || r->dc_rt == NULL) return false;
        }
        {
            MC_ID2D1RenderTarget *rt = MC_AS(ID2D1RenderTarget, r->dc_rt);
            D2D1_COLOR_F white = { 1.0f, 1.0f, 1.0f, 1.0f };
            (void)MC_CALL(rt, CreateSolidColorBrush, &white, NULL, &r->scratch_brush);
            mc_probe_color_font(r, rt);
            ++r->rt_generation;
        }
    }

    mem_dc = CreateCompatibleDC(NULL);
    if (mem_dc == NULL) return false;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)width;
    bmi.bmiHeader.biHeight = -(LONG)height; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &dib_bits, NULL, 0);
    if (dib == NULL || dib_bits == NULL) {
        DeleteDC(mem_dc);
        return false;
    }
    old_bmp = SelectObject(mem_dc, dib);

    rc.left = 0;
    rc.top = 0;
    rc.right = (LONG)width;
    rc.bottom = (LONG)height;

    saved_w = ed->viewport_w;
    saved_h = ed->viewport_h;
    ed->viewport_w = (float)width;
    ed->viewport_h = (float)height;
    mc_editor_update_wrap_width(ed);

    hr = MC_CALL(r->dc_rt, BindDC, mem_dc, &rc);
    if (SUCCEEDED(hr)) {
        hr = mc_renderer_draw_frame(ed, MC_AS(ID2D1RenderTarget, r->dc_rt),
                                    (float)width, (float)height);
        if (SUCCEEDED(hr)) {
            uint32_t row;
            for (row = 0; row < height; ++row) {
                memcpy((uint8_t *)pixels + (size_t)row * stride,
                       (const uint8_t *)dib_bits + (size_t)row * width * 4u,
                       (size_t)width * 4u);
            }
            ok = true;
        }
    }

    ed->viewport_w = saved_w;
    ed->viewport_h = saved_h;
    if (saved_w > 0.0f) mc_editor_update_wrap_width(ed);

    SelectObject(mem_dc, old_bmp);
    DeleteObject(dib);
    DeleteDC(mem_dc);
    return ok;
}

bool mc_renderer_pos_to_client_rect(monac_editor_t *ed, monac_pos_t pos,
                                    monac_rect_f_t *out_rect)
{
    mc_view_pos_t vp;
    uint32_t sub = 0;
    mc_wrap_span_t span;
    const mc_logical_line_t *ll;
    float line_h;
    float x, y;
    MC_ID2D1RenderTarget *rt = NULL;

    if (ed == NULL || out_rect == NULL) return false;
    if (ed->renderer.hwnd_rt != NULL && !ed->renderer.using_dc_rt) {
        rt = MC_AS(ID2D1RenderTarget, ed->renderer.hwnd_rt);
    }
    if (!mc_proj_model_to_view(&ed->doc.projection, (mc_pos_t){ pos.line, pos.col }, &vp))
        return false;
    ll = mc_proj_get_view_line(&ed->doc.projection, vp.view_line, &sub, &span);
    if (ll == NULL) return false;

    line_h = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
    x = mc_editor_text_left(ed) - ed->scroll_x +
        mc_view_col_to_x(ed, rt, ll, &span, vp.view_col);
    y = (float)vp.view_line * line_h - ed->scroll_y;

    out_rect->left = x;
    out_rect->top = y;
    out_rect->right = x + ed->doc.dw.space_width;
    out_rect->bottom = y + line_h;
    return true;
}

bool mc_renderer_hit_test(monac_editor_t *ed, float x, float y,
                          monac_pos_t *out_pos, uint32_t *out_fold_id,
                          bool *out_in_gutter, uint32_t *out_gutter_fold_id)
{
    float line_h;
    float gutter_w;
    float text_x0;
    uint32_t total_vlines;
    int32_t vl_signed;
    uint32_t vl;
    uint32_t sub = 0;
    mc_wrap_span_t span;
    const mc_logical_line_t *ll;
    MC_IDWriteTextLayout *layout;
    MC_ID2D1RenderTarget *rt = NULL;
    BOOL is_trailing = FALSE, is_inside = FALSE;
    DWRITE_HIT_TEST_METRICS htm;
    uint32_t view_col = 0;
    mc_pos_t mp = { 0, 0 };
    uint32_t hit_fold = 0;

    if (out_fold_id != NULL) *out_fold_id = 0;
    if (out_in_gutter != NULL) *out_in_gutter = false;
    if (out_gutter_fold_id != NULL) *out_gutter_fold_id = 0;
    if (ed == NULL || out_pos == NULL) return false;

    (void)mc_proj_ensure_valid(&ed->doc.projection);
    total_vlines = mc_proj_view_line_count(&ed->doc.projection);
    if (total_vlines == 0) {
        out_pos->line = 0;
        out_pos->col = 0;
        return true;
    }

    line_h = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
    gutter_w = mc_editor_gutter_width(ed);
    text_x0 = mc_editor_text_left(ed);

    vl_signed = (int32_t)floorf((y + ed->scroll_y) / line_h);
    if (vl_signed < 0) vl_signed = 0;
    vl = (uint32_t)vl_signed;
    if (vl >= total_vlines) vl = total_vlines - 1u;

    ll = mc_proj_get_view_line(&ed->doc.projection, vl, &sub, &span);
    if (ll == NULL) return false;

    if (x < gutter_w) {
        if (out_in_gutter != NULL) *out_in_gutter = true;
        out_pos->line = ll->first_model_line;
        out_pos->col = 0;
        if (sub == 0 && x >= gutter_w - 20.0f && out_gutter_fold_id != NULL) {
            size_t fc = mc_folding_count(&ed->doc.folding);
            if (fc > 0) {
                mc_fold_info_t *arr = (mc_fold_info_t *)malloc(fc * sizeof(mc_fold_info_t));
                if (arr != NULL) {
                    size_t i;
                    fc = mc_folding_list(&ed->doc.folding, &ed->doc.tree, arr, fc);
                    for (i = 0; i < fc; ++i) {
                        if (arr[i].range.start.line == ll->first_model_line &&
                            arr[i].range.end.line > arr[i].range.start.line) {
                            *out_gutter_fold_id = arr[i].id;
                            break;
                        }
                    }
                    free(arr);
                }
            }
        }
        return true;
    }

    if (ed->renderer.hwnd_rt != NULL && !ed->renderer.using_dc_rt) {
        rt = MC_AS(ID2D1RenderTarget, ed->renderer.hwnd_rt);
    }
    layout = mc_get_span_layout(ed, rt, ll, &span);
    if (layout != NULL) {
        float lx = (x - text_x0 + ed->scroll_x) - span.indent_x;
        if (lx < 0.0f) lx = 0.0f;
        memset(&htm, 0, sizeof(htm));
        if (SUCCEEDED(MC_CALL(layout, HitTestPoint, lx, line_h * 0.5f,
                              &is_trailing, &is_inside, &htm))) {
            /* Check if clicking directly on a fold badge (U+FFFC) */
            uint32_t hit_pos = htm.textPosition;
            uint32_t logical_pos = span.start + hit_pos;
            if (is_inside && logical_pos < ll->text.length &&
                ll->text.data[logical_pos] == (wchar_t)0xFFFCu) {
                view_col = hit_pos;
            } else {
                view_col = hit_pos + (is_trailing ? htm.length : 0u);
            }
        }
    }

    (void)mc_proj_view_to_model(&ed->doc.projection,
                                (mc_view_pos_t){ vl, view_col },
                                &mp, &hit_fold);
    out_pos->line = mp.line;
    out_pos->col = mp.col;
    if (out_fold_id != NULL) {
        /* Only report fold badge click if actually inside badge or directly on it */
        *out_fold_id = (is_inside || hit_fold != 0) ? hit_fold : 0u;
    }
    return true;
}
