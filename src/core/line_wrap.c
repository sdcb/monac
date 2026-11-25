#include "line_wrap.h"

#include "../base/utf.h"
#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>

void mc_wrap_result_init(mc_wrap_result_t *res)
{
    if (res == NULL) return;
    memset(res, 0, sizeof(*res));
}

void mc_wrap_result_destroy(mc_wrap_result_t *res)
{
    if (res == NULL) return;
    free(res->spans);
    free(res->chunks);
    memset(res, 0, sizeof(*res));
}

static int mc_wrap_push_span(mc_wrap_result_t *res, uint32_t start, uint32_t len,
                             float indent_x, uint32_t chunk_index)
{
    if (!mc_vec_reserve((void **)&res->spans, &res->span_cap,
                        sizeof(mc_wrap_span_t), res->span_count + 1)) {
        return 0;
    }
    res->spans[res->span_count].start = start;
    res->spans[res->span_count].length = len;
    res->spans[res->span_count].indent_x = indent_x;
    res->spans[res->span_count].chunk_index = chunk_index;
    ++res->span_count;
    return 1;
}

static int mc_wrap_push_chunk(mc_wrap_result_t *res, uint32_t start, uint32_t len)
{
    if (!mc_vec_reserve((void **)&res->chunks, &res->chunk_cap,
                        sizeof(mc_wrap_chunk_t), res->chunk_count + 1)) {
        return 0;
    }
    res->chunks[res->chunk_count].start = start;
    res->chunks[res->chunk_count].length = len;
    ++res->chunk_count;
    return 1;
}

static float mc_compute_leading_indent_x(const mc_dw_context *dw,
                                         const wchar_t *text, uint32_t len,
                                         float max_width)
{
    uint32_t i;
    float x = 0.0f;
    float tab_px = dw->space_width * (float)(dw->tab_size > 0 ? dw->tab_size : 4);
    for (i = 0; i < len; ++i) {
        if (text[i] == L' ') {
            x += dw->space_width;
        } else if (text[i] == L'\t') {
            float rem = tab_px > 0.0f ? (float)((int)(x / tab_px) + 1) * tab_px : x + 4.0f;
            x = rem;
        } else {
            break;
        }
    }
    if (x > max_width * 0.5f) return 0.0f;
    return x;
}

static int mc_is_chunk_break_char(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L',' || ch == L';' ||
           ch == L':' || ch == L')' || ch == L']' || ch == L'}';
}

static int mc_split_chunks(const wchar_t *text, uint32_t length,
                           uint32_t chunk_size, mc_wrap_result_t *res)
{
    uint32_t pos = 0;
    if (chunk_size < 64) chunk_size = MC_DEFAULT_LONG_LINE_CHUNK;
    if (length == 0) {
        return mc_wrap_push_chunk(res, 0, 0);
    }
    while (pos < length) {
        uint32_t end = (length - pos <= chunk_size) ? length : (pos + chunk_size);
        if (end < length) {
            uint32_t search_min = (end > pos + 256) ? (end - 256) : (pos + 1);
            uint32_t k;
            for (k = end; k > search_min; --k) {
                if (mc_is_chunk_break_char(text[k - 1])) {
                    end = k;
                    break;
                }
            }
            if (!mc_utf16_is_boundary(text, length, end) && end > pos + 1) {
                --end;
            }
        }
        if (!mc_wrap_push_chunk(res, pos, end - pos)) return 0;
        pos = end;
    }
    return 1;
}

static void mc_attach_inlines(MC_IDWriteTextLayout *layout,
                              const mc_wrap_inline_t *inlines, size_t inline_count,
                              uint32_t seg_start, uint32_t seg_len)
{
    size_t i;
    for (i = 0; i < inline_count; ++i) {
        if (inlines[i].object != NULL &&
            inlines[i].pos >= seg_start &&
            inlines[i].pos < seg_start + seg_len) {
            DWRITE_TEXT_RANGE r;
            r.startPosition = inlines[i].pos - seg_start;
            r.length = 1;
            (void)MC_CALL(layout, SetInlineObject, inlines[i].object, r);
        }
    }
}

static int mc_measure_layout_lines(MC_IDWriteTextLayout *layout,
                                   uint32_t base_offset,
                                   float indent_x,
                                   uint32_t chunk_idx,
                                   mc_wrap_result_t *res)
{
    DWRITE_LINE_METRICS stack_metrics[32];
    DWRITE_LINE_METRICS *metrics = stack_metrics;
    UINT32 actual = 0;
    UINT32 i;
    uint32_t cursor = base_offset;
    HRESULT hr;

    hr = MC_CALL(layout, GetLineMetrics, stack_metrics, 32, &actual);
    if (hr == E_NOT_SUFFICIENT_BUFFER && actual > 0) {
        metrics = (DWRITE_LINE_METRICS *)malloc(actual * sizeof(DWRITE_LINE_METRICS));
        if (metrics == NULL) return 0;
        hr = MC_CALL(layout, GetLineMetrics, metrics, actual, &actual);
    }
    if (FAILED(hr) || actual == 0) {
        if (metrics != stack_metrics) free(metrics);
        return 0;
    }
    for (i = 0; i < actual; ++i) {
        if (!mc_wrap_push_span(res, cursor, metrics[i].length, indent_x, chunk_idx)) {
            if (metrics != stack_metrics) free(metrics);
            return 0;
        }
        cursor += metrics[i].length;
    }
    if (metrics != stack_metrics) free(metrics);
    return 1;
}

int mc_wrap_compute(mc_dw_context *dw,
                    const wchar_t *text, uint32_t length,
                    const mc_wrap_inline_t *inlines, size_t inline_count,
                    float max_width, mc_wrap_mode_t mode,
                    uint32_t chunk_size, mc_wrap_result_t *out)
{
    size_t c;
    float indent_x = 0.0f;

    if (dw == NULL || out == NULL) return 0;
    out->span_count = 0;
    out->chunk_count = 0;
    out->wrap_width = max_width;
    out->mode = mode;
    out->font_version = dw->font_version;

    if (!mc_split_chunks(text, length, chunk_size, out)) return 0;

    if (mode == MC_WRAP_OFF || max_width <= 8.0f || length == 0) {
        return mc_wrap_push_span(out, 0, length, 0.0f, 0);
    }

    indent_x = mc_compute_leading_indent_x(dw, text, length, max_width);

    for (c = 0; c < out->chunk_count; ++c) {
        uint32_t c_start = out->chunks[c].start;
        uint32_t c_len = out->chunks[c].length;
        float first_indent = (c == 0) ? 0.0f : indent_x;
        float first_width = max_width - first_indent;
        MC_IDWriteTextLayout *layout;

        if (first_width < 8.0f) first_width = 8.0f;
        layout = mc_dw_create_layout(dw, text + c_start, c_len, first_width, true);
        if (layout == NULL) {
            if (!mc_wrap_push_span(out, c_start, c_len, first_indent, (uint32_t)c))
                return 0;
            continue;
        }
        mc_attach_inlines(layout, inlines, inline_count, c_start, c_len);

        if (c == 0 && indent_x > 0.0f) {
            /* Line 0 has indent 0, continuation lines have indent_x */
            DWRITE_LINE_METRICS m0[4];
            UINT32 actual = 0;
            HRESULT hr = MC_CALL(layout, GetLineMetrics, m0, 4, &actual);
            if ((SUCCEEDED(hr) || hr == E_NOT_SUFFICIENT_BUFFER) && actual >= 1) {
                uint32_t len0 = m0[0].length;
                if (len0 > c_len) len0 = c_len;
                MC_SAFE_RELEASE(layout);
                if (!mc_wrap_push_span(out, c_start, len0, 0.0f, 0)) return 0;
                if (len0 < c_len) {
                    float cont_w = max_width - indent_x;
                    MC_IDWriteTextLayout *rest;
                    if (cont_w < 8.0f) cont_w = 8.0f;
                    rest = mc_dw_create_layout(dw, text + c_start + len0,
                                               c_len - len0, cont_w, true);
                    if (rest != NULL) {
                        mc_attach_inlines(rest, inlines, inline_count,
                                          c_start + len0, c_len - len0);
                        (void)mc_measure_layout_lines(rest, c_start + len0,
                                                      indent_x, 0, out);
                        MC_SAFE_RELEASE(rest);
                    } else {
                        if (!mc_wrap_push_span(out, c_start + len0, c_len - len0,
                                               indent_x, 0)) return 0;
                    }
                }
            } else {
                MC_SAFE_RELEASE(layout);
                if (!mc_wrap_push_span(out, c_start, c_len, 0.0f, 0)) return 0;
            }
        } else {
            if (!mc_measure_layout_lines(layout, c_start, first_indent, (uint32_t)c, out)) {
                MC_SAFE_RELEASE(layout);
                if (!mc_wrap_push_span(out, c_start, c_len, first_indent, (uint32_t)c))
                    return 0;
            } else {
                MC_SAFE_RELEASE(layout);
            }
        }
    }

    if (out->span_count == 0) {
        return mc_wrap_push_span(out, 0, length, 0.0f, 0);
    }
    return 1;
}
