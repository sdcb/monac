#include "projection.h"

#include "../base/vec.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void mc_logical_line_release_inlines(mc_logical_line_t *ll)
{
    size_t i;
    for (i = 0; i < ll->inline_count; ++i) {
        MC_SAFE_RELEASE(ll->inlines[i].object);
    }
    ll->inline_count = 0;
}

static void mc_logical_line_destroy(mc_logical_line_t *ll)
{
    mc_logical_line_release_inlines(ll);
    mc_wbuf_destroy(&ll->text);
    free(ll->segments);
    free(ll->inlines);
    mc_wrap_result_destroy(&ll->wrap);
    memset(ll, 0, sizeof(*ll));
}

static void mc_logical_line_reset_contents(mc_logical_line_t *ll)
{
    mc_logical_line_release_inlines(ll);
    mc_wbuf_clear(&ll->text);
    ll->seg_count = 0;
    ll->wrap.span_count = 0;
    ll->wrap.chunk_count = 0;
    ll->built = false;
}

static int mc_ll_push_seg(mc_logical_line_t *ll, const mc_proj_segment_t *seg)
{
    if (!mc_vec_reserve((void **)&ll->segments, &ll->seg_cap,
                        sizeof(mc_proj_segment_t), ll->seg_count + 1)) {
        return 0;
    }
    ll->segments[ll->seg_count++] = *seg;
    return 1;
}

static int mc_ll_push_inline(mc_logical_line_t *ll, uint32_t pos,
                             MC_IDWriteInlineObject *obj)
{
    if (!mc_vec_reserve((void **)&ll->inlines, &ll->inline_cap,
                        sizeof(mc_wrap_inline_t), ll->inline_count + 1)) {
        MC_SAFE_RELEASE(obj);
        return 0;
    }
    ll->inlines[ll->inline_count].pos = pos;
    ll->inlines[ll->inline_count].object = obj;
    ++ll->inline_count;
    return 1;
}

static int mc_inlay_cmp(const void *a, const void *b)
{
    const mc_inlay_entry_t *ia = (const mc_inlay_entry_t *)a;
    const mc_inlay_entry_t *ib = (const mc_inlay_entry_t *)b;
    return mc_pos_cmp(ia->pos, ib->pos);
}

void mc_proj_init(mc_projection *proj, const mc_piece_tree *tree,
                  mc_folding *folding, mc_dw_context *dw)
{
    if (proj == NULL) return;
    memset(proj, 0, sizeof(*proj));
    proj->tree = tree;
    proj->folding = folding;
    proj->dw = dw;
    mc_fenwick_init(&proj->fenwick);
    proj->wrap_mode = MC_WRAP_OFF;
    proj->wrap_width = 0.0f;
    proj->long_line_chunk = MC_DEFAULT_LONG_LINE_CHUNK;
    proj->next_line_version = 1;
    proj->dirty_all = true;
}

void mc_proj_destroy(mc_projection *proj)
{
    size_t i;
    if (proj == NULL) return;
    for (i = 0; i < proj->line_cap; ++i) {
        mc_logical_line_destroy(&proj->lines[i]);
    }
    free(proj->lines);
    free(proj->model_to_logical);
    for (i = 0; i < proj->inlay_count; ++i) {
        free(proj->inlays[i].text);
    }
    free(proj->inlays);
    mc_fenwick_destroy(&proj->fenwick);
    memset(proj, 0, sizeof(*proj));
}

void mc_proj_set_inline_draw_state(mc_projection *proj,
                                   mc_inline_draw_state_t *draw_state)
{
    if (proj == NULL) return;
    proj->inline_draw_state = draw_state;
    proj->dirty_all = true;
}

void mc_proj_set_wrap(mc_projection *proj, mc_wrap_mode_t mode, float width)
{
    if (proj == NULL) return;
    if (proj->wrap_mode == mode && fabsf(proj->wrap_width - width) < 0.5f)
        return;
    proj->wrap_mode = mode;
    proj->wrap_width = width;
    proj->dirty_all = true;
}

void mc_proj_set_chunk_size(mc_projection *proj, uint32_t chunk_size)
{
    if (proj == NULL || chunk_size < 64) return;
    if (proj->long_line_chunk == chunk_size) return;
    proj->long_line_chunk = chunk_size;
    proj->dirty_all = true;
}

int mc_proj_set_inlays(mc_projection *proj, uint32_t owner_id,
                       const mc_inlay_spec_t *items, size_t count)
{
    size_t i;
    size_t write = 0;
    if (proj == NULL || (items == NULL && count != 0)) return 0;

    for (i = 0; i < proj->inlay_count; ++i) {
        if (proj->inlays[i].owner_id == owner_id) {
            free(proj->inlays[i].text);
        } else {
            if (write != i) proj->inlays[write] = proj->inlays[i];
            ++write;
        }
    }
    proj->inlay_count = write;

    if (count > 0) {
        if (!mc_vec_reserve((void **)&proj->inlays, &proj->inlay_cap,
                            sizeof(mc_inlay_entry_t), proj->inlay_count + count)) {
            return 0;
        }
        for (i = 0; i < count; ++i) {
            const wchar_t *src = items[i].text ? items[i].text : L"";
            size_t len = wcslen(src);
            wchar_t *copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
            if (copy == NULL) return 0;
            memcpy(copy, src, (len + 1) * sizeof(wchar_t));
            proj->inlays[proj->inlay_count].owner_id = owner_id;
            proj->inlays[proj->inlay_count].pos = items[i].pos;
            proj->inlays[proj->inlay_count].text = copy;
            proj->inlays[proj->inlay_count].fg_color = items[i].fg_color;
            proj->inlays[proj->inlay_count].bg_color = items[i].bg_color;
            ++proj->inlay_count;
        }
    }
    if (proj->inlay_count > 1) {
        qsort(proj->inlays, proj->inlay_count, sizeof(mc_inlay_entry_t), mc_inlay_cmp);
    }
    proj->dirty_all = true;
    return 1;
}

void mc_proj_invalidate_all(mc_projection *proj)
{
    if (proj != NULL) proj->dirty_all = true;
}

static int mc_build_simple_line(mc_projection *proj, mc_logical_line_t *ll)
{
    mc_proj_segment_t seg;
    mc_logical_line_reset_contents(ll);
    if (!mc_pt_get_line(proj->tree, ll->first_model_line, &ll->text)) return 0;

    memset(&seg, 0, sizeof(seg));
    seg.kind = MC_SEG_TEXT;
    seg.view_col_start = 0;
    seg.view_col_len = (uint32_t)ll->text.length;
    seg.model_line = ll->first_model_line;
    seg.model_col_start = 0;
    seg.model_col_len = (uint32_t)ll->text.length;
    if (!mc_ll_push_seg(ll, &seg)) return 0;

    if (!mc_wrap_compute(proj->dw, ll->text.data ? ll->text.data : L"",
                         (uint32_t)ll->text.length, NULL, 0,
                         proj->wrap_width, proj->wrap_mode,
                         proj->long_line_chunk, &ll->wrap)) {
        return 0;
    }
    ll->version = proj->next_line_version++;
    ll->built = true;
    return 1;
}

static int mc_ensure_line_built(mc_projection *proj, mc_logical_line_t *ll)
{
    if (ll->built) return 1;
    return mc_build_simple_line(proj, ll);
}

void mc_proj_invalidate_model_line(mc_projection *proj, uint32_t model_line)
{
    if (proj == NULL) return;
    if (proj->dirty_all || proj->tree == NULL) {
        proj->dirty_all = true;
        return;
    }
    if (mc_pt_line_count(proj->tree) != proj->model_line_count ||
        model_line >= proj->model_line_count) {
        proj->dirty_all = true;
        return;
    }
    {
        uint32_t lidx = proj->model_to_logical[model_line];
        mc_logical_line_t *ll = &proj->lines[lidx];
        if (ll->has_specials || ll->first_model_line != ll->last_model_line) {
            proj->dirty_all = true;
            return;
        }
        ll->built = false;
        if (proj->wrap_mode == MC_WRAP_WORD) {
            if (mc_build_simple_line(proj, ll)) {
                mc_fenwick_set(&proj->fenwick, ll->first_model_line,
                               (uint32_t)ll->wrap.span_count);
            } else {
                proj->dirty_all = true;
            }
        } else {
            ll->version = proj->next_line_version++;
        }
    }
}

static int mc_ensure_lines_cap(mc_projection *proj, size_t needed)
{
    size_t old_cap = proj->line_cap;
    if (needed <= old_cap) return 1;
    if (!mc_vec_reserve((void **)&proj->lines, &proj->line_cap,
                        sizeof(mc_logical_line_t), needed)) {
        return 0;
    }
    memset(proj->lines + old_cap, 0,
           (proj->line_cap - old_cap) * sizeof(mc_logical_line_t));
    return 1;
}

static int mc_build_complex_logical_line(mc_projection *proj,
                                         mc_logical_line_t *ll,
                                         uint32_t start_line,
                                         const mc_fold_info_t *folds, size_t fold_count,
                                         size_t *fold_idx,
                                         size_t *inlay_idx,
                                         mc_wbuf *line_scratch)
{
    uint32_t cur_line = start_line;
    uint32_t cur_col = 0;

    mc_logical_line_reset_contents(ll);
    ll->first_model_line = start_line;
    ll->last_model_line = start_line;
    ll->has_specials = true;

    for (;;) {
        uint32_t line_len;
        if (!mc_pt_get_line(proj->tree, cur_line, line_scratch)) return 0;
        line_len = (uint32_t)line_scratch->length;
        if (cur_col > line_len) cur_col = line_len;

        /* Skip inlays strictly before (cur_line, cur_col) */
        while (*inlay_idx < proj->inlay_count &&
               mc_pos_cmp(proj->inlays[*inlay_idx].pos,
                          (mc_pos_t){ cur_line, cur_col }) < 0) {
            ++(*inlay_idx);
        }
        /* Skip folds strictly before (cur_line, cur_col) */
        while (*fold_idx < fold_count &&
               mc_pos_cmp(folds[*fold_idx].range.start,
                          (mc_pos_t){ cur_line, cur_col }) < 0) {
            ++(*fold_idx);
        }

        for (;;) {
            bool has_inlay = (*inlay_idx < proj->inlay_count &&
                              proj->inlays[*inlay_idx].pos.line == cur_line);
            bool has_fold = (*fold_idx < fold_count &&
                             folds[*fold_idx].range.start.line == cur_line);
            uint32_t next_inlay_col = has_inlay ? proj->inlays[*inlay_idx].pos.col : UINT32_MAX;
            uint32_t next_fold_col = has_fold ? folds[*fold_idx].range.start.col : UINT32_MAX;

            if (has_inlay && next_inlay_col > line_len) next_inlay_col = line_len;
            if (has_fold && next_fold_col > line_len) next_fold_col = line_len;

            if (!has_inlay && !has_fold) {
                /* Emit remaining text on cur_line */
                uint32_t rem = line_len - cur_col;
                if (rem > 0 || ll->seg_count == 0) {
                    mc_proj_segment_t seg;
                    memset(&seg, 0, sizeof(seg));
                    seg.kind = MC_SEG_TEXT;
                    seg.view_col_start = (uint32_t)ll->text.length;
                    seg.view_col_len = rem;
                    seg.model_line = cur_line;
                    seg.model_col_start = cur_col;
                    seg.model_col_len = rem;
                    if (rem > 0 && !mc_wbuf_append(&ll->text, line_scratch->data + cur_col, rem))
                        return 0;
                    if (!mc_ll_push_seg(ll, &seg)) return 0;
                }
                ll->last_model_line = cur_line;
                goto done_segments;
            }

            if (has_inlay && (!has_fold || next_inlay_col <= next_fold_col)) {
                /* Emit text before inlay */
                if (next_inlay_col > cur_col) {
                    uint32_t len = next_inlay_col - cur_col;
                    mc_proj_segment_t seg;
                    memset(&seg, 0, sizeof(seg));
                    seg.kind = MC_SEG_TEXT;
                    seg.view_col_start = (uint32_t)ll->text.length;
                    seg.view_col_len = len;
                    seg.model_line = cur_line;
                    seg.model_col_start = cur_col;
                    seg.model_col_len = len;
                    if (!mc_wbuf_append(&ll->text, line_scratch->data + cur_col, len))
                        return 0;
                    if (!mc_ll_push_seg(ll, &seg)) return 0;
                    cur_col = next_inlay_col;
                }
                /* Emit inlay segment + U+FFFC inline object */
                {
                    const mc_inlay_entry_t *inlay = &proj->inlays[*inlay_idx];
                    mc_proj_segment_t seg;
                    uint32_t vpos = (uint32_t)ll->text.length;
                    MC_IDWriteInlineObject *obj;
                    memset(&seg, 0, sizeof(seg));
                    seg.kind = MC_SEG_INLAY;
                    seg.view_col_start = vpos;
                    seg.view_col_len = 1;
                    seg.inlay_pos = (mc_pos_t){ cur_line, cur_col };
                    seg.inlay_index = (uint32_t)(*inlay_idx);
                    if (!mc_wbuf_append_char(&ll->text, (wchar_t)0xFFFCu)) return 0;
                    if (!mc_ll_push_seg(ll, &seg)) return 0;
                    obj = mc_inline_badge_create(proj->dw, MC_INLINE_INLAY, 0,
                                                 inlay->text,
                                                 inlay->fg_color, inlay->bg_color, 0,
                                                 proj->inline_draw_state);
                    if (obj != NULL && !mc_ll_push_inline(ll, vpos, obj)) return 0;
                }
                ++(*inlay_idx);
            } else {
                /* Collapsed fold starts at (cur_line, next_fold_col) */
                const mc_fold_info_t *fold = &folds[*fold_idx];
                if (next_fold_col > cur_col) {
                    uint32_t len = next_fold_col - cur_col;
                    mc_proj_segment_t seg;
                    memset(&seg, 0, sizeof(seg));
                    seg.kind = MC_SEG_TEXT;
                    seg.view_col_start = (uint32_t)ll->text.length;
                    seg.view_col_len = len;
                    seg.model_line = cur_line;
                    seg.model_col_start = cur_col;
                    seg.model_col_len = len;
                    if (!mc_wbuf_append(&ll->text, line_scratch->data + cur_col, len))
                        return 0;
                    if (!mc_ll_push_seg(ll, &seg)) return 0;
                    cur_col = next_fold_col;
                }
                {
                    mc_proj_segment_t seg;
                    uint32_t vpos = (uint32_t)ll->text.length;
                    MC_IDWriteInlineObject *obj;
                    memset(&seg, 0, sizeof(seg));
                    seg.kind = MC_SEG_FOLD_PLACEHOLDER;
                    seg.view_col_start = vpos;
                    seg.view_col_len = 1;
                    seg.fold_id = fold->id;
                    seg.fold_start = fold->range.start;
                    seg.fold_end = fold->range.end;
                    if (!mc_wbuf_append_char(&ll->text, (wchar_t)0xFFFCu)) return 0;
                    if (!mc_ll_push_seg(ll, &seg)) return 0;
                    obj = mc_inline_badge_create(proj->dw, MC_INLINE_FOLD_BADGE,
                                                 fold->id, fold->placeholder,
                                                 0, 0, 0, proj->inline_draw_state);
                    if (obj != NULL && !mc_ll_push_inline(ll, vpos, obj)) return 0;
                }
                ++(*fold_idx);
                /* Jump to fold end; skip inlays inside fold */
                while (*inlay_idx < proj->inlay_count &&
                       mc_pos_cmp(proj->inlays[*inlay_idx].pos, fold->range.end) < 0) {
                    ++(*inlay_idx);
                }
                if (fold->range.end.line != cur_line) {
                    cur_line = fold->range.end.line;
                    cur_col = fold->range.end.col;
                    break; /* reload line_scratch for new cur_line */
                } else {
                    cur_col = fold->range.end.col;
                }
            }
        }
    }

done_segments:
    if (!mc_wrap_compute(proj->dw, ll->text.data ? ll->text.data : L"",
                         (uint32_t)ll->text.length,
                         ll->inlines, ll->inline_count,
                         proj->wrap_width, proj->wrap_mode,
                         proj->long_line_chunk, &ll->wrap)) {
        return 0;
    }
    ll->version = proj->next_line_version++;
    ll->built = true;
    return 1;
}

int mc_proj_ensure_valid(mc_projection *proj)
{
    size_t model_lines;
    mc_fold_info_t *folds = NULL;
    size_t fold_count = 0;
    size_t fold_idx = 0;
    size_t inlay_idx = 0;
    uint32_t *fw_counts = NULL;
    uint32_t mline = 0;
    size_t lcount = 0;
    mc_wbuf scratch;

    if (proj == NULL || proj->tree == NULL || proj->dw == NULL) return 0;
    if (!proj->dirty_all && proj->built_font_version == proj->dw->font_version &&
        mc_pt_line_count(proj->tree) == proj->model_line_count) {
        return 1;
    }

    model_lines = mc_pt_line_count(proj->tree);
    if (model_lines == 0) model_lines = 1;

    if (!mc_vec_reserve((void **)&proj->model_to_logical,
                        &proj->model_to_logical_cap,
                        sizeof(uint32_t), model_lines)) {
        return 0;
    }
    fw_counts = (uint32_t *)calloc(model_lines, sizeof(uint32_t));
    if (fw_counts == NULL) return 0;

    if (proj->folding != NULL) {
        fold_count = mc_folding_list_top_collapsed(proj->folding, proj->tree, NULL, 0);
        if (fold_count > 0) {
            folds = (mc_fold_info_t *)malloc(fold_count * sizeof(mc_fold_info_t));
            if (folds == NULL) {
                free(fw_counts);
                return 0;
            }
            fold_count = mc_folding_list_top_collapsed(proj->folding, proj->tree,
                                                       folds, fold_count);
        }
    }

    mc_wbuf_init(&scratch);
    while ((size_t)mline < model_lines) {
        bool has_fold = false;
        bool has_inlay = false;
        mc_logical_line_t *ll;

        while (fold_idx < fold_count && folds[fold_idx].range.start.line < mline)
            ++fold_idx;
        while (inlay_idx < proj->inlay_count && proj->inlays[inlay_idx].pos.line < mline)
            ++inlay_idx;

        if (fold_idx < fold_count && folds[fold_idx].range.start.line == mline)
            has_fold = true;
        if (inlay_idx < proj->inlay_count && proj->inlays[inlay_idx].pos.line == mline)
            has_inlay = true;

        if (!mc_ensure_lines_cap(proj, lcount + 1)) {
            mc_wbuf_destroy(&scratch);
            free(folds);
            free(fw_counts);
            return 0;
        }
        ll = &proj->lines[lcount];

        if (!has_fold && !has_inlay) {
            mc_logical_line_reset_contents(ll);
            ll->first_model_line = mline;
            ll->last_model_line = mline;
            ll->has_specials = false;
            proj->model_to_logical[mline] = (uint32_t)lcount;
            if (proj->wrap_mode == MC_WRAP_WORD) {
                if (!mc_build_simple_line(proj, ll)) {
                    mc_wbuf_destroy(&scratch);
                    free(folds);
                    free(fw_counts);
                    return 0;
                }
                fw_counts[mline] = (uint32_t)ll->wrap.span_count;
            } else {
                ll->version = proj->next_line_version++;
                fw_counts[mline] = 1;
            }
            ++mline;
        } else {
            uint32_t m;
            if (!mc_build_complex_logical_line(proj, ll, mline,
                                               folds, fold_count, &fold_idx,
                                               &inlay_idx, &scratch)) {
                mc_wbuf_destroy(&scratch);
                free(folds);
                free(fw_counts);
                return 0;
            }
            if (ll->last_model_line >= (uint32_t)model_lines)
                ll->last_model_line = (uint32_t)(model_lines - 1);
            for (m = ll->first_model_line; m <= ll->last_model_line; ++m) {
                proj->model_to_logical[m] = (uint32_t)lcount;
                fw_counts[m] = (m == ll->first_model_line)
                                   ? (uint32_t)ll->wrap.span_count
                                   : 0;
            }
            mline = ll->last_model_line + 1;
        }
        ++lcount;
    }

    mc_wbuf_destroy(&scratch);
    free(folds);

    /* Release excess previously used lines */
    {
        size_t k;
        for (k = lcount; k < proj->line_count; ++k) {
            mc_logical_line_reset_contents(&proj->lines[k]);
        }
    }

    proj->line_count = lcount;
    proj->model_line_count = model_lines;
    if (!mc_fenwick_reset(&proj->fenwick, model_lines, fw_counts)) {
        free(fw_counts);
        return 0;
    }
    free(fw_counts);
    proj->built_font_version = proj->dw->font_version;
    proj->dirty_all = false;
    return 1;
}

uint32_t mc_proj_view_line_count(mc_projection *proj)
{
    if (!mc_proj_ensure_valid(proj)) return 0;
    return mc_fenwick_total(&proj->fenwick);
}

const mc_logical_line_t *mc_proj_get_view_line(mc_projection *proj,
                                               uint32_t view_line,
                                               uint32_t *out_sub_line,
                                               mc_wrap_span_t *out_span)
{
    uint32_t mline = 0;
    uint32_t sub = 0;
    mc_logical_line_t *ll;

    if (!mc_proj_ensure_valid(proj)) return NULL;
    if (!mc_fenwick_find_view_line(&proj->fenwick, view_line, &mline, &sub))
        return NULL;
    ll = &proj->lines[proj->model_to_logical[mline]];
    if (!mc_ensure_line_built(proj, ll)) return NULL;
    if (sub >= ll->wrap.span_count) sub = (uint32_t)(ll->wrap.span_count - 1);
    if (out_sub_line != NULL) *out_sub_line = sub;
    if (out_span != NULL) *out_span = ll->wrap.spans[sub];
    return ll;
}

bool mc_proj_model_to_view(mc_projection *proj, mc_pos_t model_pos,
                           mc_view_pos_t *out_view_pos)
{
    mc_logical_line_t *ll;
    uint32_t logical_col = 0;
    bool matched = false;
    size_t i;
    uint32_t base_view_line;

    if (proj == NULL || out_view_pos == NULL || !mc_proj_ensure_valid(proj))
        return false;

    if (model_pos.line >= proj->model_line_count)
        model_pos.line = (uint32_t)(proj->model_line_count - 1);

    ll = &proj->lines[proj->model_to_logical[model_pos.line]];
    if (!mc_ensure_line_built(proj, ll)) return false;

    /* Find logical_col across segments */
    for (i = 0; i < ll->seg_count; ++i) {
        const mc_proj_segment_t *seg = &ll->segments[i];
        if (seg->kind == MC_SEG_TEXT) {
            if (seg->model_line == model_pos.line &&
                model_pos.col >= seg->model_col_start &&
                model_pos.col <= seg->model_col_start + seg->model_col_len) {
                logical_col = seg->view_col_start + (model_pos.col - seg->model_col_start);
                matched = true;
                break;
            }
        } else if (seg->kind == MC_SEG_FOLD_PLACEHOLDER) {
            if (mc_pos_cmp(seg->fold_start, model_pos) <= 0 &&
                mc_pos_cmp(model_pos, seg->fold_end) < 0) {
                /* Inside collapsed fold -> snap to placeholder start boundary */
                logical_col = seg->view_col_start;
                matched = true;
                break;
            }
            if (mc_pos_cmp(model_pos, seg->fold_end) == 0) {
                logical_col = seg->view_col_start + seg->view_col_len;
                matched = true;
                /* Keep scanning in case a text segment starts at fold_end (same col) */
            }
        } else if (seg->kind == MC_SEG_INLAY) {
            if (mc_pos_cmp(seg->inlay_pos, model_pos) == 0 && !matched) {
                logical_col = seg->view_col_start;
                matched = true;
            }
        }
    }
    if (!matched) {
        logical_col = (uint32_t)ll->text.length;
    }

    base_view_line = mc_fenwick_prefix_sum(&proj->fenwick, ll->first_model_line);
    for (i = 0; i < ll->wrap.span_count; ++i) {
        const mc_wrap_span_t *sp = &ll->wrap.spans[i];
        bool is_last = (i + 1 == ll->wrap.span_count);
        if (logical_col >= sp->start &&
            (logical_col < sp->start + sp->length ||
             (is_last && logical_col <= sp->start + sp->length))) {
            out_view_pos->view_line = base_view_line + (uint32_t)i;
            out_view_pos->view_col = logical_col - sp->start;
            return true;
        }
    }
    out_view_pos->view_line = base_view_line;
    out_view_pos->view_col = 0;
    return true;
}

bool mc_proj_view_to_model(mc_projection *proj, mc_view_pos_t view_pos,
                           mc_pos_t *out_model_pos, uint32_t *out_hit_fold_id)
{
    uint32_t sub = 0;
    mc_wrap_span_t span;
    const mc_logical_line_t *ll;
    uint32_t logical_col;
    size_t i;

    if (out_hit_fold_id != NULL) *out_hit_fold_id = 0;
    if (proj == NULL || out_model_pos == NULL || !mc_proj_ensure_valid(proj))
        return false;

    {
        uint32_t total = mc_fenwick_total(&proj->fenwick);
        if (total == 0) {
            out_model_pos->line = 0;
            out_model_pos->col = 0;
            return true;
        }
        if (view_pos.view_line >= total) view_pos.view_line = total - 1;
    }

    ll = mc_proj_get_view_line(proj, view_pos.view_line, &sub, &span);
    if (ll == NULL) return false;

    logical_col = span.start + (view_pos.view_col <= span.length ? view_pos.view_col : span.length);

    /* Check exact hit on a fold placeholder or inlay first */
    for (i = 0; i < ll->seg_count; ++i) {
        const mc_proj_segment_t *seg = &ll->segments[i];
        if (seg->kind == MC_SEG_FOLD_PLACEHOLDER &&
            logical_col == seg->view_col_start) {
            *out_model_pos = seg->fold_start;
            if (out_hit_fold_id != NULL) *out_hit_fold_id = seg->fold_id;
            return true;
        }
        if (seg->kind == MC_SEG_INLAY &&
            logical_col == seg->view_col_start) {
            *out_model_pos = seg->inlay_pos;
            return true;
        }
    }

    for (i = 0; i < ll->seg_count; ++i) {
        const mc_proj_segment_t *seg = &ll->segments[i];
        if (seg->kind == MC_SEG_TEXT &&
            logical_col >= seg->view_col_start &&
            logical_col <= seg->view_col_start + seg->view_col_len) {
            out_model_pos->line = seg->model_line;
            out_model_pos->col = seg->model_col_start + (logical_col - seg->view_col_start);
            return true;
        }
        if (seg->kind == MC_SEG_FOLD_PLACEHOLDER &&
            logical_col == seg->view_col_start + seg->view_col_len) {
            *out_model_pos = seg->fold_end;
            return true;
        }
        if (seg->kind == MC_SEG_INLAY &&
            logical_col == seg->view_col_start + seg->view_col_len) {
            *out_model_pos = seg->inlay_pos;
            return true;
        }
    }

    out_model_pos->line = ll->last_model_line;
    out_model_pos->col = (uint32_t)mc_pt_line_length(proj->tree, ll->last_model_line);
    return true;
}
