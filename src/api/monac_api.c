#include "editor_internal.h"
#include "base/vec.h"
#include "lexer/lexer_json.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void mc_init_default_theme(monac_theme_t *t)
{
    memset(t, 0, sizeof(*t));
    t->struct_size = sizeof(*t);
    t->bg = 0xFF1E1E2Eu;
    t->fg = 0xFFCDD6F4u;
    t->current_line_bg = 0xFF262637u;
    t->selection_bg = 0xFF3B4A6Bu;
    t->gutter_bg = 0xFF181825u;
    t->gutter_fg = 0xFF6C7086u;
    t->gutter_active_fg = 0xFFCDD6F4u;
    t->indent_guide = 0xFF313244u;
    t->caret = 0xFFF5E0DCu;
    t->scrollbar_thumb = 0x806C7086u;
    t->badge_bg = 0xFF2C3E55u;
    t->badge_fg = 0xFFD8DEE9u;
    t->badge_border = 0xFF4C6A92u;
}

static void mc_init_default_token_styles(monac_token_style_t styles[MC_MAX_TOKEN_STYLES])
{
    memset(styles, 0, sizeof(monac_token_style_t) * MC_MAX_TOKEN_STYLES);
    styles[MC_TOK_DEFAULT].fg_color = 0xFFCDD6F4u;
    styles[MC_TOK_JSON_KEY].fg_color = 0xFF89B4FAu;
    styles[MC_TOK_JSON_STRING].fg_color = 0xFFA6E3A1u;
    styles[MC_TOK_JSON_NUMBER].fg_color = 0xFFFAB387u;
    styles[MC_TOK_JSON_BOOL].fg_color = 0xFFCBA6F7u;
    styles[MC_TOK_JSON_NULL].fg_color = 0xFFCBA6F7u;
    styles[MC_TOK_JSON_PUNCT].fg_color = 0xFF9399B2u;
    styles[MC_TOK_JSON_COMMENT].fg_color = 0xFF6C7086u;
    styles[MC_TOK_JSON_COMMENT].italic = true;
    styles[MC_TOK_JSON_ERROR].fg_color = 0xFFF38BA8u;
    styles[MC_TOK_KEYWORD].fg_color = 0xFFCBA6F7u;
    styles[MC_TOK_TYPE].fg_color = 0xFFF9E2AFu;
    styles[MC_TOK_FUNCTION].fg_color = 0xFF89B4FAu;
    styles[MC_TOK_VARIABLE].fg_color = 0xFFCDD6F4u;
    styles[MC_TOK_PROPERTY].fg_color = 0xFF94E2D5u;
    styles[MC_TOK_OPERATOR].fg_color = 0xFF89DCEBu;
    styles[MC_TOK_PREPROC].fg_color = 0xFFF5C2E7u;
}

float mc_editor_scrollbar_width(void)
{
    return 12.0f;
}

float mc_editor_gutter_width(const monac_editor_t *ed)
{
    if (ed == NULL) return 0.0f;
    if (!ed->show_line_numbers && mc_folding_count(&ed->doc.folding) == 0)
        return 16.0f;
    {
        size_t lines = mc_pt_line_count(&ed->doc.tree);
        int digits = 3;
        size_t n = lines;
        while (n >= 1000) {
            ++digits;
            n /= 10;
        }
        return 16.0f + (float)digits * ed->doc.dw.space_width + 24.0f;
    }
}

float mc_editor_text_left(const monac_editor_t *ed)
{
    return mc_editor_gutter_width(ed) + 8.0f;
}

void mc_editor_invalidate_view(monac_editor_t *ed)
{
    if (ed != NULL && ed->hwnd != NULL) {
        InvalidateRect(ed->hwnd, NULL, FALSE);
    }
}

void mc_editor_update_wrap_width(monac_editor_t *ed)
{
    float avail;
    if (ed == NULL) return;
    avail = ed->viewport_w - mc_editor_text_left(ed) - mc_editor_scrollbar_width() - 8.0f;
    if (avail < 40.0f) avail = 40.0f;
    mc_proj_set_wrap(&ed->doc.projection, ed->doc.projection.wrap_mode, avail);
}

void mc_editor_ensure_caret_visible(monac_editor_t *ed)
{
    mc_selection_t pri;
    mc_view_pos_t vp = { 0, 0 };
    float line_h;
    float cy;
    if (ed == NULL || ed->viewport_h <= 1.0f) return;
    pri = mc_cursors_primary(&ed->doc.cursors);
    if (!mc_proj_model_to_view(&ed->doc.projection, pri.active, &vp)) return;
    line_h = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
    cy = (float)vp.view_line * line_h;
    if (cy < ed->scroll_y) {
        ed->scroll_y = cy;
    } else if (cy + line_h > ed->scroll_y + ed->viewport_h) {
        ed->scroll_y = cy + line_h - ed->viewport_h;
    }
    if (ed->scroll_y < 0.0f) ed->scroll_y = 0.0f;
}

void mc_editor_notify_text_changed(monac_editor_t *ed, uint64_t version_id,
                                   uint32_t first_dirty_line)
{
    if (ed == NULL) return;
    mc_lexer_cache_invalidate_lines(&ed->lexer, first_dirty_line,
                                    first_dirty_line,
                                    mc_pt_line_count(&ed->doc.tree));
    ++ed->syntax_version;
    mc_renderer_invalidate_layouts(ed);
    mc_overlays_on_text_changed(ed);
    mc_editor_invalidate_view(ed);
    mc_uia_raise_text_changed(ed);
    if (ed->callbacks.on_text_changed != NULL) {
        ed->callbacks.on_text_changed(ed->callbacks_ud, ed, version_id);
    }
}

void mc_editor_notify_cursor_changed(monac_editor_t *ed)
{
    mc_selection_t pri;
    if (ed == NULL) return;
    pri = mc_cursors_primary(&ed->doc.cursors);
    mc_ime_update_position(ed);
    mc_uia_raise_selection_changed(ed);
    if (ed->callbacks.on_cursor_changed != NULL) {
        ed->callbacks.on_cursor_changed(
            ed->callbacks_ud, ed,
            (monac_pos_t){ pri.active.line, pri.active.col });
    }
}

void mc_editor_notify_fold_toggled(monac_editor_t *ed, uint32_t fold_id, bool collapsed)
{
    if (ed == NULL) return;
    mc_proj_invalidate_all(&ed->doc.projection);
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
    if (ed->callbacks.on_fold_toggled != NULL) {
        ed->callbacks.on_fold_toggled(ed->callbacks_ud, ed, fold_id, collapsed);
    }
}

static void mc_doc_text_cb_bridge(void *ud, uint64_t version_id, uint32_t first_dirty_line)
{
    mc_editor_notify_text_changed((monac_editor_t *)ud, version_id, first_dirty_line);
}

static void mc_doc_cursor_cb_bridge(void *ud)
{
    mc_editor_notify_cursor_changed((monac_editor_t *)ud);
}

static void mc_fold_cb_bridge(void *ud, uint32_t fold_id, bool collapsed)
{
    mc_editor_notify_fold_toggled((monac_editor_t *)ud, fold_id, collapsed);
}

static uint32_t mc_custom_lexer_init_bridge(void *ud)
{
    monac_editor_t *ed = (monac_editor_t *)ud;
    if (ed->custom_lexer.initial_state != NULL)
        return ed->custom_lexer.initial_state(ed->custom_lexer_ud);
    return 0;
}

static uint32_t mc_custom_lexer_tok_bridge(void *ud, uint32_t in_state,
                                           const wchar_t *line, size_t len,
                                           mc_token_t *out_tokens, size_t *inout_count)
{
    monac_editor_t *ed = (monac_editor_t *)ud;
    if (ed->custom_lexer.tokenize_line == NULL) {
        if (inout_count != NULL) *inout_count = 0;
        return 0;
    }
    _Static_assert(sizeof(monac_token_t) == sizeof(mc_token_t), "token layout");
    return ed->custom_lexer.tokenize_line(ed->custom_lexer_ud, in_state,
                                          line, len,
                                          (monac_token_t *)out_tokens, inout_count);
}

MONAC_API monac_editor_t *monac_create(const monac_options_t *opts)
{
    monac_editor_t *ed = (monac_editor_t *)calloc(1, sizeof(monac_editor_t));
    if (ed == NULL) return NULL;

    if (!mc_doc_init(&ed->doc)) {
        free(ed);
        return NULL;
    }
    mc_lexer_cache_init(&ed->lexer);
    mc_ime_init(&ed->ime);
    mc_init_default_theme(&ed->theme);
    mc_init_default_token_styles(ed->token_styles);

    ed->show_line_numbers = true;
    ed->show_indent_guides = true;
    ed->dpi = 96.0f;
    ed->viewport_w = 800.0f;
    ed->viewport_h = 600.0f;
    ed->caret_visible = true;
    ed->syntax_version = 1;

    ed->doc.on_text_changed = mc_doc_text_cb_bridge;
    ed->doc.on_text_changed_ud = ed;
    ed->doc.on_cursor_changed = mc_doc_cursor_cb_bridge;
    ed->doc.on_cursor_changed_ud = ed;
    mc_folding_set_toggled_cb(&ed->doc.folding, mc_fold_cb_bridge, ed);

    if (!mc_renderer_init(ed)) {
        mc_ime_destroy(&ed->ime);
        mc_lexer_cache_destroy(&ed->lexer);
        mc_doc_destroy(&ed->doc);
        free(ed);
        return NULL;
    }
    mc_overlays_init(ed);
    if (opts != NULL) {
        monac_set_options(ed, opts);
    } else {
        mc_editor_update_wrap_width(ed);
    }
    return ed;
}

MONAC_API void monac_destroy(monac_editor_t *ed)
{
    if (ed == NULL) return;
    mc_uia_detach(ed);
    if (ed->hwnd != NULL) {
        HWND h = ed->hwnd;
        ed->owns_hwnd_destroy = false;
        ed->hwnd = NULL;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        DestroyWindow(h);
    }
    mc_overlays_destroy(ed);
    mc_renderer_destroy(ed);
    mc_ime_destroy(&ed->ime);
    mc_lexer_cache_destroy(&ed->lexer);
    free(ed->semantic_tokens);
    mc_doc_destroy(&ed->doc);
    free(ed);
}

MONAC_API void monac_set_options(monac_editor_t *ed, const monac_options_t *opts)
{
    if (ed == NULL || opts == NULL) return;
    if (opts->tab_size >= 1 && opts->tab_size <= 16) {
        ed->doc.tab_size = opts->tab_size;
    }
    ed->doc.insert_spaces = opts->insert_spaces;
    ed->show_line_numbers = opts->show_line_numbers;
    ed->show_indent_guides = opts->show_indent_guides;
    mc_pt_set_eol_mode(&ed->doc.tree,
                       opts->eol_mode == MONAC_EOL_CRLF ? MC_EOL_CRLF : MC_EOL_LF);

    if ((opts->font_family != NULL && opts->font_family[0] != L'\0') ||
        opts->font_size >= 6.0f || opts->tab_size >= 1) {
        (void)mc_dw_set_font(&ed->doc.dw, opts->font_family,
                             opts->font_size, ed->doc.tab_size);
    }
    if (opts->theme != NULL) {
        if (opts->theme->bg) ed->theme.bg = opts->theme->bg;
        if (opts->theme->fg) ed->theme.fg = opts->theme->fg;
        if (opts->theme->current_line_bg) ed->theme.current_line_bg = opts->theme->current_line_bg;
        if (opts->theme->selection_bg) ed->theme.selection_bg = opts->theme->selection_bg;
        if (opts->theme->gutter_bg) ed->theme.gutter_bg = opts->theme->gutter_bg;
        if (opts->theme->gutter_fg) ed->theme.gutter_fg = opts->theme->gutter_fg;
        if (opts->theme->gutter_active_fg) ed->theme.gutter_active_fg = opts->theme->gutter_active_fg;
        if (opts->theme->indent_guide) ed->theme.indent_guide = opts->theme->indent_guide;
        if (opts->theme->caret) ed->theme.caret = opts->theme->caret;
        if (opts->theme->scrollbar_thumb) ed->theme.scrollbar_thumb = opts->theme->scrollbar_thumb;
    }

    ed->doc.projection.wrap_mode =
        (opts->wrap_mode == MONAC_WRAP_WORD) ? MC_WRAP_WORD : MC_WRAP_OFF;
    mc_editor_update_wrap_width(ed);
    mc_proj_invalidate_all(&ed->doc.projection);
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API monac_wrap_mode_t monac_get_wrap_mode(const monac_editor_t *ed)
{
    if (ed == NULL) return MONAC_WRAP_OFF;
    return ed->doc.projection.wrap_mode == MC_WRAP_WORD ? MONAC_WRAP_WORD : MONAC_WRAP_OFF;
}

MONAC_API void monac_set_wrap_mode(monac_editor_t *ed, monac_wrap_mode_t mode)
{
    if (ed == NULL) return;
    ed->doc.projection.wrap_mode = (mode == MONAC_WRAP_WORD) ? MC_WRAP_WORD : MC_WRAP_OFF;
    mc_editor_update_wrap_width(ed);
    mc_proj_invalidate_all(&ed->doc.projection);
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API void monac_set_text(monac_editor_t *ed, const wchar_t *text, size_t len)
{
    if (ed == NULL) return;
    ed->semantic_count = 0;
    ed->scroll_y = 0.0f;
    ed->scroll_x = 0.0f;
    (void)mc_doc_set_text(&ed->doc, text ? text : L"", len);
    mc_lexer_cache_invalidate_all(&ed->lexer);
    if (ed->lexer.has_lexer && ed->lexer.vtable.extract_folds != NULL) {
        (void)monac_fold_refresh_from_lexer(ed);
    }
    mc_editor_invalidate_view(ed);
}

MONAC_API size_t monac_get_text(const monac_editor_t *ed, wchar_t *buf, size_t cap)
{
    if (ed == NULL) return 0;
    return mc_doc_get_text(&ed->doc, buf, cap);
}

MONAC_API size_t monac_get_line(const monac_editor_t *ed, uint32_t line,
                                wchar_t *buf, size_t cap)
{
    if (ed == NULL) return 0;
    return mc_pt_get_line_raw(&ed->doc.tree, line, buf, cap);
}

MONAC_API size_t monac_get_line_count(const monac_editor_t *ed)
{
    if (ed == NULL) return 0;
    return mc_pt_line_count(&ed->doc.tree);
}

MONAC_API uint64_t monac_get_version(const monac_editor_t *ed)
{
    if (ed == NULL) return 0;
    return mc_doc_version(&ed->doc);
}

MONAC_API void monac_apply_edits(monac_editor_t *ed,
                                 const monac_text_edit_t *edits, size_t count)
{
    mc_range_t *ranges;
    const wchar_t **texts;
    size_t *lens;
    size_t i;
    if (ed == NULL || edits == NULL || count == 0) return;

    ranges = (mc_range_t *)calloc(count, sizeof(mc_range_t));
    texts = (const wchar_t **)calloc(count, sizeof(const wchar_t *));
    lens = (size_t *)calloc(count, sizeof(size_t));
    if (ranges && texts && lens) {
        for (i = 0; i < count; ++i) {
            ranges[i].start.line = edits[i].range.start.line;
            ranges[i].start.col = edits[i].range.start.col;
            ranges[i].end.line = edits[i].range.end.line;
            ranges[i].end.col = edits[i].range.end.col;
            texts[i] = edits[i].text;
            lens[i] = edits[i].len;
        }
        (void)mc_doc_apply_range_edits(&ed->doc, ranges, texts, lens, count, GetTickCount64());
    }
    free(ranges);
    free(texts);
    free(lens);
}

MONAC_API void monac_undo(monac_editor_t *ed)
{
    if (ed == NULL) return;
    if (mc_doc_undo(&ed->doc)) {
        mc_editor_ensure_caret_visible(ed);
        mc_editor_invalidate_view(ed);
    }
}

MONAC_API void monac_redo(monac_editor_t *ed)
{
    if (ed == NULL) return;
    if (mc_doc_redo(&ed->doc)) {
        mc_editor_ensure_caret_visible(ed);
        mc_editor_invalidate_view(ed);
    }
}

MONAC_API size_t monac_get_selections(const monac_editor_t *ed,
                                      monac_selection_t *out, size_t cap)
{
    size_t i, n;
    if (ed == NULL) return 0;
    n = ed->doc.cursors.count;
    if (out != NULL) {
        size_t c = n < cap ? n : cap;
        for (i = 0; i < c; ++i) {
            out[i].anchor.line = ed->doc.cursors.items[i].anchor.line;
            out[i].anchor.col = ed->doc.cursors.items[i].anchor.col;
            out[i].active.line = ed->doc.cursors.items[i].active.line;
            out[i].active.col = ed->doc.cursors.items[i].active.col;
        }
    }
    return n;
}

MONAC_API void monac_set_selections(monac_editor_t *ed,
                                    const monac_selection_t *sels, size_t count)
{
    mc_selection_t *tmp = NULL;
    size_t i;
    if (ed == NULL) return;
    if (sels != NULL && count > 0) {
        tmp = (mc_selection_t *)calloc(count, sizeof(mc_selection_t));
        if (tmp == NULL) return;
        for (i = 0; i < count; ++i) {
            tmp[i].anchor.line = sels[i].anchor.line;
            tmp[i].anchor.col = sels[i].anchor.col;
            tmp[i].active.line = sels[i].active.line;
            tmp[i].active.col = sels[i].active.col;
        }
    }
    mc_doc_set_selections(&ed->doc, tmp, tmp ? count : 0);
    free(tmp);
    mc_editor_ensure_caret_visible(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API bool monac_add_next_occurrence(monac_editor_t *ed)
{
    bool ok;
    if (ed == NULL) return false;
    ok = mc_cursors_add_next_occurrence(&ed->doc.cursors, &ed->doc.tree);
    if (ok) {
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
    }
    return ok;
}

MONAC_API void monac_insert_text_at_cursors(monac_editor_t *ed,
                                            const wchar_t *text, size_t len)
{
    if (ed == NULL) return;
    ed->doc.dirty_first_line = UINT32_MAX;
    if (mc_cursors_insert_text(&ed->doc.cursors, &ed->doc.edit_stack,
                               text, len, MC_EDIT_OTHER, GetTickCount64())) {
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_text_changed(
            ed, mc_doc_version(&ed->doc),
            ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
        mc_editor_notify_cursor_changed(ed);
    }
}

MONAC_API uint32_t monac_fold_add(monac_editor_t *ed, monac_range_t range,
                                  const wchar_t *placeholder, bool collapsed)
{
    uint32_t id;
    if (ed == NULL) return 0;
    id = mc_doc_fold_add(&ed->doc,
                         (mc_range_t){ { range.start.line, range.start.col },
                                       { range.end.line, range.end.col } },
                         placeholder, collapsed);
    if (id != 0) {
        mc_renderer_invalidate_layouts(ed);
        mc_editor_invalidate_view(ed);
    }
    return id;
}

MONAC_API void monac_fold_set_collapsed(monac_editor_t *ed,
                                        uint32_t fold_id, bool collapsed)
{
    if (ed == NULL) return;
    mc_doc_fold_set_collapsed(&ed->doc, fold_id, collapsed);
}

MONAC_API bool monac_fold_toggle_at(monac_editor_t *ed, monac_pos_t pos)
{
    if (ed == NULL) return false;
    return mc_doc_fold_toggle_at(&ed->doc, (mc_pos_t){ pos.line, pos.col });
}

MONAC_API void monac_fold_all(monac_editor_t *ed, bool collapsed)
{
    if (ed == NULL) return;
    mc_doc_fold_all(&ed->doc, collapsed);
}

MONAC_API void monac_fold_clear(monac_editor_t *ed)
{
    if (ed == NULL) return;
    mc_doc_fold_clear(&ed->doc);
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API size_t monac_fold_get_count(const monac_editor_t *ed)
{
    if (ed == NULL) return 0;
    return mc_folding_count(&ed->doc.folding);
}

MONAC_API bool monac_fold_is_collapsed(const monac_editor_t *ed, uint32_t fold_id)
{
    mc_fold_info_t info;
    if (ed == NULL) return false;
    if (!mc_folding_get((mc_folding *)&ed->doc.folding, &ed->doc.tree, fold_id, &info))
        return false;
    return info.collapsed;
}

MONAC_API size_t monac_fold_refresh_from_lexer(monac_editor_t *ed)
{
    size_t n = 0;
    size_t added = 0;
    size_t i;
    if (ed == NULL) return 0;

    if (ed->using_custom_lexer && ed->custom_lexer.fold_ranges != NULL) {
        n = ed->custom_lexer.fold_ranges(ed->custom_lexer_ud, ed, NULL, 0);
        if (n > 0) {
            monac_fold_range_t *arr = (monac_fold_range_t *)calloc(n, sizeof(monac_fold_range_t));
            if (arr != NULL) {
                n = ed->custom_lexer.fold_ranges(ed->custom_lexer_ud, ed, arr, n);
                mc_doc_fold_clear(&ed->doc);
                for (i = 0; i < n; ++i) {
                    if (monac_fold_add(ed, arr[i].range, arr[i].placeholder, false) != 0)
                        ++added;
                }
                free(arr);
            }
        }
        return added;
    }

    n = mc_lexer_extract_folds(&ed->lexer, &ed->doc.tree, NULL, 0);
    mc_doc_fold_clear(&ed->doc);
    if (n > 0) {
        mc_fold_candidate_t *arr = (mc_fold_candidate_t *)calloc(n, sizeof(mc_fold_candidate_t));
        if (arr != NULL) {
            n = mc_lexer_extract_folds(&ed->lexer, &ed->doc.tree, arr, n);
            for (i = 0; i < n; ++i) {
                if (mc_doc_fold_add(&ed->doc, arr[i].range, arr[i].placeholder, false) != 0)
                    ++added;
            }
            free(arr);
        }
    }
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
    return added;
}

MONAC_API void monac_inlay_set(monac_editor_t *ed, uint32_t owner_id,
                               const monac_inlay_t *items, size_t count)
{
    mc_inlay_spec_t *specs = NULL;
    size_t i;
    if (ed == NULL) return;
    if (items != NULL && count > 0) {
        specs = (mc_inlay_spec_t *)calloc(count, sizeof(mc_inlay_spec_t));
        if (specs == NULL) return;
        for (i = 0; i < count; ++i) {
            specs[i].pos.line = items[i].pos.line;
            specs[i].pos.col = items[i].pos.col;
            specs[i].text = items[i].text;
            specs[i].fg_color = items[i].fg_color;
            specs[i].bg_color = items[i].bg_color;
        }
    }
    (void)mc_proj_set_inlays(&ed->doc.projection, owner_id, specs, specs ? count : 0);
    free(specs);
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API size_t monac_get_view_line_count(const monac_editor_t *ed)
{
    if (ed == NULL) return 0;
    return mc_proj_view_line_count((mc_projection *)&ed->doc.projection);
}

MONAC_API size_t monac_get_view_line_text(const monac_editor_t *ed, uint32_t view_line,
                                          wchar_t *buf, size_t cap)
{
    mc_wrap_span_t span;
    const mc_logical_line_t *ll;
    if (ed == NULL) return 0;
    ll = mc_proj_get_view_line((mc_projection *)&ed->doc.projection, view_line, NULL, &span);
    if (ll == NULL) return 0;
    if (buf != NULL && cap > 0 && ll->text.data != NULL) {
        size_t n = span.length < cap ? span.length : cap;
        if (span.start + n > ll->text.length) {
            n = span.start < ll->text.length ? (ll->text.length - span.start) : 0;
        }
        if (n > 0) memcpy(buf, ll->text.data + span.start, n * sizeof(wchar_t));
        if (n < cap) buf[n] = L'\0';
    }
    return span.length;
}

MONAC_API bool monac_model_to_view(const monac_editor_t *ed, monac_pos_t pos,
                                   monac_view_pos_t *out)
{
    mc_view_pos_t vp = { 0, 0 };
    if (ed == NULL || out == NULL) return false;
    if (!mc_proj_model_to_view((mc_projection *)&ed->doc.projection,
                               (mc_pos_t){ pos.line, pos.col }, &vp)) {
        return false;
    }
    out->view_line = vp.view_line;
    out->view_col = vp.view_col;
    return true;
}

MONAC_API bool monac_view_to_model(const monac_editor_t *ed, monac_view_pos_t view_pos,
                                   monac_pos_t *out_pos, uint32_t *out_fold_id)
{
    mc_pos_t mp = { 0, 0 };
    if (ed == NULL || out_pos == NULL) return false;
    if (!mc_proj_view_to_model((mc_projection *)&ed->doc.projection,
                               (mc_view_pos_t){ view_pos.view_line, view_pos.view_col },
                               &mp, out_fold_id)) {
        return false;
    }
    out_pos->line = mp.line;
    out_pos->col = mp.col;
    return true;
}

MONAC_API void monac_decorations_set(monac_editor_t *ed, uint32_t owner_id,
                                     const monac_decoration_t *items, size_t count)
{
    mc_dec_spec_t *specs = NULL;
    size_t i;
    if (ed == NULL || owner_id == MC_OWNER_FOLDING) return;
    if (items != NULL && count > 0) {
        specs = (mc_dec_spec_t *)calloc(count, sizeof(mc_dec_spec_t));
        if (specs == NULL) return;
        for (i = 0; i < count; ++i) {
            size_t s = 0, e = 0;
            mc_range_t r = mc_range_make(
                (mc_pos_t){ items[i].range.start.line, items[i].range.start.col },
                (mc_pos_t){ items[i].range.end.line, items[i].range.end.col });
            (void)mc_pt_pos_to_offset(&ed->doc.tree, mc_pos_clamp(&ed->doc.tree, r.start), &s);
            (void)mc_pt_pos_to_offset(&ed->doc.tree, mc_pos_clamp(&ed->doc.tree, r.end), &e);
            specs[i].start = s;
            specs[i].end = e;
            specs[i].stickiness = (mc_stickiness_t)items[i].stickiness;
            specs[i].collapse_on_delete = items[i].collapse_on_delete;
            specs[i].style.fg_color = items[i].fg_color;
            specs[i].style.bg_color = items[i].bg_color;
            specs[i].style.border_color = items[i].border_color;
            specs[i].style.underline_color = items[i].underline_color;
            specs[i].style.underline_style = (mc_underline_style_t)items[i].underline_style;
            specs[i].style.gutter_color = items[i].gutter_color;
            specs[i].style.gutter_icon = items[i].gutter_icon;
            specs[i].style.hover_text = items[i].hover_text;
        }
    }
    (void)mc_dec_replace_owner(&ed->doc.decorations, owner_id, specs, specs ? count : 0);
    free(specs);
    mc_editor_invalidate_view(ed);
}

MONAC_API size_t monac_decorations_count(const monac_editor_t *ed, uint32_t owner_id)
{
    if (ed == NULL) return 0;
    return mc_dec_query_owner((mc_dec_tree *)&ed->doc.decorations, owner_id,
                              0, SIZE_MAX / 2, NULL, 0);
}

MONAC_API size_t monac_get_decoration_hover_at(const monac_editor_t *ed, monac_pos_t pos,
                                               wchar_t *buf, size_t cap)
{
    size_t off = 0;
    mc_dec_snapshot_t hits[32];
    size_t cnt, i;
    mc_wbuf acc;
    size_t out_len;
    if (ed == NULL) return 0;
    if (!mc_pt_pos_to_offset(&ed->doc.tree, (mc_pos_t){ pos.line, pos.col }, &off))
        return 0;
    cnt = mc_dec_query((mc_dec_tree *)&ed->doc.decorations, off, off, hits, 32);
    mc_wbuf_init(&acc);
    for (i = 0; i < cnt && i < 32; ++i) {
        if (hits[i].style.hover_text != NULL && hits[i].style.hover_text[0] != L'\0') {
            if (acc.length > 0) (void)mc_wbuf_append_char(&acc, L'\n');
            (void)mc_wbuf_append_wcstr(&acc, hits[i].style.hover_text);
        }
    }
    out_len = acc.length;
    if (buf != NULL && cap > 0) {
        size_t n = out_len < cap - 1 ? out_len : cap - 1;
        if (n > 0 && acc.data != NULL) memcpy(buf, acc.data, n * sizeof(wchar_t));
        buf[n] = L'\0';
    }
    mc_wbuf_destroy(&acc);
    return out_len;
}

MONAC_API void monac_set_language(monac_editor_t *ed, const wchar_t *lang_id)
{
    if (ed == NULL) return;
    ed->using_custom_lexer = false;
    if (lang_id != NULL && (_wcsicmp(lang_id, L"json") == 0 ||
                            _wcsicmp(lang_id, L"jsonc") == 0)) {
        mc_lexer_cache_set_lexer(&ed->lexer, mc_lexer_json_vtable(), NULL);
        (void)monac_fold_refresh_from_lexer(ed);
    } else {
        mc_lexer_cache_set_lexer(&ed->lexer, NULL, NULL);
    }
    ++ed->syntax_version;
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API void monac_set_custom_lexer(monac_editor_t *ed,
                                      const monac_lexer_vtable_t *vtable,
                                      void *userdata)
{
    if (ed == NULL) return;
    if (vtable == NULL || vtable->tokenize_line == NULL) {
        ed->using_custom_lexer = false;
        mc_lexer_cache_set_lexer(&ed->lexer, NULL, NULL);
    } else {
        mc_lexer_vtable_t bridge;
        ed->custom_lexer = *vtable;
        ed->custom_lexer_ud = userdata;
        ed->using_custom_lexer = true;
        bridge.initial_state = mc_custom_lexer_init_bridge;
        bridge.tokenize_line = mc_custom_lexer_tok_bridge;
        bridge.extract_folds = NULL;
        mc_lexer_cache_set_lexer(&ed->lexer, &bridge, ed);
    }
    ++ed->syntax_version;
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API void monac_set_token_style(monac_editor_t *ed, uint32_t style_id,
                                     monac_token_style_t style)
{
    if (ed == NULL || style_id >= MC_MAX_TOKEN_STYLES) return;
    ed->token_styles[style_id] = style;
    ++ed->syntax_version;
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API void monac_set_semantic_tokens(monac_editor_t *ed,
                                         const monac_semantic_token_t *tokens,
                                         size_t count)
{
    if (ed == NULL) return;
    ed->semantic_count = 0;
    if (tokens != NULL && count > 0) {
        if (mc_vec_reserve((void **)&ed->semantic_tokens, &ed->semantic_cap,
                           sizeof(monac_semantic_token_t), count)) {
            memcpy(ed->semantic_tokens, tokens, count * sizeof(monac_semantic_token_t));
            ed->semantic_count = count;
        }
    }
    ++ed->syntax_version;
    mc_renderer_invalidate_layouts(ed);
    mc_editor_invalidate_view(ed);
}

MONAC_API size_t monac_get_line_token_count(const monac_editor_t *ed, uint32_t line)
{
    const mc_line_tokens_t *lt;
    if (ed == NULL) return 0;
    (void)mc_lexer_cache_ensure_line((mc_lexer_cache *)&ed->lexer, &ed->doc.tree, line);
    lt = mc_lexer_cache_get_line(&ed->lexer, line);
    return lt ? lt->count : 0;
}

MONAC_API bool monac_pos_to_client_rect(const monac_editor_t *ed, monac_pos_t pos,
                                        monac_rect_f_t *out_rect)
{
    return mc_renderer_pos_to_client_rect((monac_editor_t *)ed, pos, out_rect);
}

MONAC_API bool monac_client_point_to_pos(const monac_editor_t *ed, float x, float y,
                                         monac_pos_t *out_pos, uint32_t *out_fold_id)
{
    return mc_renderer_hit_test((monac_editor_t *)ed, x, y, out_pos, out_fold_id, NULL, NULL);
}

MONAC_API void monac_set_callbacks(monac_editor_t *ed,
                                   const monac_callbacks_t *cb, void *userdata)
{
    if (ed == NULL) return;
    if (cb != NULL) {
        ed->callbacks = *cb;
        ed->callbacks_ud = userdata;
    } else {
        memset(&ed->callbacks, 0, sizeof(ed->callbacks));
        ed->callbacks_ud = NULL;
    }
}

MONAC_API bool monac_render_to_bgra(monac_editor_t *ed,
                                    uint32_t width, uint32_t height,
                                    void *pixels, uint32_t stride)
{
    return mc_renderer_render_to_bgra(ed, width, height, pixels, stride);
}

static int mc_double_cmp(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return da < db ? -1 : (da > db ? 1 : 0);
}

MONAC_API void monac_get_frame_stats(const monac_editor_t *ed,
                                     monac_frame_stats_t *out)
{
    double sorted[MC_FRAME_STATS_CAP];
    uint32_t n, i;
    double sum = 0.0;
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (ed == NULL || ed->renderer.frame_count == 0) return;
    n = ed->renderer.frame_count;
    for (i = 0; i < n; ++i) {
        sorted[i] = ed->renderer.frame_times_ms[i];
        sum += sorted[i];
    }
    qsort(sorted, n, sizeof(double), mc_double_cmp);
    out->frame_count = n;
    out->min_ms = sorted[0];
    out->max_ms = sorted[n - 1];
    out->avg_ms = sum / (double)n;
    {
        uint32_t p95_idx = (uint32_t)((double)(n - 1) * 0.95);
        out->p95_ms = sorted[p95_idx];
    }
}
