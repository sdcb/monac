#include "dw_text.h"

#include <math.h>
#include <string.h>

static int mc_dw_rebuild_formats(mc_dw_context *ctx)
{
    MC_IDWriteTextFormat *fmt = NULL;
    MC_IDWriteTextFormat *bfmt = NULL;
    HRESULT hr;
    float w = 8.0f, h = 16.0f;
    float badge_size;

    if (ctx == NULL || ctx->factory == NULL) return 0;

    hr = MC_CALL(ctx->factory, CreateTextFormat,
                 ctx->font_family, NULL,
                 DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                 DWRITE_FONT_STRETCH_NORMAL, ctx->font_size, L"", &fmt);
    if (FAILED(hr) || fmt == NULL) {
        hr = MC_CALL(ctx->factory, CreateTextFormat,
                     L"Consolas", NULL,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, ctx->font_size, L"", &fmt);
        if (FAILED(hr) || fmt == NULL) return 0;
    }

    (void)MC_CALL(fmt, SetTextAlignment, DWRITE_TEXT_ALIGNMENT_LEADING);
    (void)MC_CALL(fmt, SetParagraphAlignment, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    (void)MC_CALL(fmt, SetWordWrapping, DWRITE_WORD_WRAPPING_NO_WRAP);

    /* Measure monospace cell width & line height */
    if (mc_dw_measure_text(ctx, fmt, L"00000000", 8, &w, &h) && w > 0.0f) {
        ctx->space_width = w / 8.0f;
    } else {
        ctx->space_width = ctx->font_size * 0.6f;
    }
    ctx->line_height = ceilf(ctx->font_size * 1.45f);
    if (ctx->line_height < h + 2.0f) ctx->line_height = ceilf(h + 2.0f);
    ctx->baseline = floorf(ctx->line_height * 0.78f);

    (void)MC_CALL(fmt, SetLineSpacing,
                  DWRITE_LINE_SPACING_METHOD_UNIFORM,
                  ctx->line_height, ctx->baseline);
    (void)MC_CALL(fmt, SetIncrementalTabStop,
                  ctx->space_width * (float)(ctx->tab_size > 0 ? ctx->tab_size : 4));

    badge_size = ctx->font_size * 0.85f;
    if (badge_size < 9.0f) badge_size = 9.0f;
    hr = MC_CALL(ctx->factory, CreateTextFormat,
                 ctx->font_family, NULL,
                 DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                 DWRITE_FONT_STRETCH_NORMAL, badge_size, L"", &bfmt);
    if (SUCCEEDED(hr) && bfmt != NULL) {
        (void)MC_CALL(bfmt, SetTextAlignment, DWRITE_TEXT_ALIGNMENT_LEADING);
        (void)MC_CALL(bfmt, SetParagraphAlignment, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        (void)MC_CALL(bfmt, SetWordWrapping, DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    MC_SAFE_RELEASE(ctx->text_format);
    MC_SAFE_RELEASE(ctx->badge_format);
    ctx->text_format = fmt;
    ctx->badge_format = bfmt;
    ++ctx->font_version;
    return 1;
}

int mc_dw_init(mc_dw_context *ctx)
{
    HRESULT hr;
    if (ctx == NULL) return 0;
    memset(ctx, 0, sizeof(*ctx));
    wcsncpy(ctx->font_family, L"Consolas", MC_ARRAY_COUNT(ctx->font_family) - 1);
    ctx->font_size = 14.0f;
    ctx->tab_size = 4;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             &IID_IDWriteFactory,
                             (MC_IUnknown **)&ctx->factory);
    if (FAILED(hr) || ctx->factory == NULL) return 0;

    if (!mc_dw_rebuild_formats(ctx)) {
        MC_SAFE_RELEASE(ctx->factory);
        return 0;
    }
    return 1;
}

void mc_dw_destroy(mc_dw_context *ctx)
{
    if (ctx == NULL) return;
    MC_SAFE_RELEASE(ctx->badge_format);
    MC_SAFE_RELEASE(ctx->text_format);
    MC_SAFE_RELEASE(ctx->factory);
    memset(ctx, 0, sizeof(*ctx));
}

int mc_dw_set_font(mc_dw_context *ctx, const wchar_t *family,
                   float font_size, uint32_t tab_size)
{
    if (ctx == NULL || ctx->factory == NULL) return 0;
    if (family != NULL && family[0] != L'\0') {
        wcsncpy(ctx->font_family, family, MC_ARRAY_COUNT(ctx->font_family) - 1);
        ctx->font_family[MC_ARRAY_COUNT(ctx->font_family) - 1] = L'\0';
    }
    if (font_size >= 6.0f && font_size <= 96.0f) {
        ctx->font_size = font_size;
    }
    if (tab_size >= 1 && tab_size <= 16) {
        ctx->tab_size = tab_size;
    }
    return mc_dw_rebuild_formats(ctx);
}

MC_IDWriteTextLayout *mc_dw_create_layout(mc_dw_context *ctx,
                                          const wchar_t *text, uint32_t len,
                                          float max_width, bool word_wrap)
{
    MC_IDWriteTextLayout *layout = NULL;
    HRESULT hr;
    float mw = (word_wrap && max_width > 1.0f) ? max_width : 1000000.0f;
    if (ctx == NULL || ctx->factory == NULL || ctx->text_format == NULL)
        return NULL;
    hr = MC_CALL(ctx->factory, CreateTextLayout,
                 text ? text : L"", len, ctx->text_format,
                 mw, 1000000.0f, &layout);
    if (FAILED(hr) || layout == NULL) return NULL;
    (void)MC_CALL(layout, SetWordWrapping,
                  word_wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    return layout;
}

MC_IDWriteTextLayout *mc_dw_create_badge_layout(mc_dw_context *ctx,
                                                const wchar_t *text, uint32_t len)
{
    MC_IDWriteTextLayout *layout = NULL;
    MC_IDWriteTextFormat *fmt;
    HRESULT hr;
    if (ctx == NULL || ctx->factory == NULL) return NULL;
    fmt = ctx->badge_format != NULL ? ctx->badge_format : ctx->text_format;
    if (fmt == NULL) return NULL;
    hr = MC_CALL(ctx->factory, CreateTextLayout,
                 text ? text : L"", len, fmt,
                 10000.0f, ctx->line_height, &layout);
    if (FAILED(hr)) return NULL;
    return layout;
}

int mc_dw_measure_text(mc_dw_context *ctx, MC_IDWriteTextFormat *fmt,
                       const wchar_t *text, uint32_t len,
                       float *out_width, float *out_height)
{
    MC_IDWriteTextLayout *layout = NULL;
    DWRITE_TEXT_METRICS metrics;
    HRESULT hr;
    if (ctx == NULL || ctx->factory == NULL || fmt == NULL) return 0;
    hr = MC_CALL(ctx->factory, CreateTextLayout,
                 text ? text : L"", len, fmt, 100000.0f, 100000.0f, &layout);
    if (FAILED(hr) || layout == NULL) return 0;
    memset(&metrics, 0, sizeof(metrics));
    hr = MC_CALL(layout, GetMetrics, &metrics);
    MC_SAFE_RELEASE(layout);
    if (FAILED(hr)) return 0;
    if (out_width != NULL) *out_width = metrics.widthIncludingTrailingWhitespace;
    if (out_height != NULL) *out_height = metrics.height;
    return 1;
}
