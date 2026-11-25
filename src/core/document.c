#include "document.h"

#include <stdlib.h>
#include <string.h>

static void mc_doc_on_edit_delta(void *userdata, const mc_edit_delta_t *delta)
{
    mc_document *doc = (mc_document *)userdata;
    mc_pos_t pos = { 0, 0 };
    if (doc == NULL || delta == NULL) return;

    mc_folding_will_replace(&doc->folding, delta->offset,
                            delta->deleted_len, delta->inserted_len);
    mc_dec_accept_replace(&doc->decorations, delta->offset,
                          delta->deleted_len, delta->inserted_len);
    mc_folding_did_replace(&doc->folding);

    if (mc_pt_offset_to_pos(&doc->tree,
                            delta->offset <= mc_pt_length(&doc->tree)
                                ? delta->offset
                                : mc_pt_length(&doc->tree),
                            &pos)) {
        if (pos.line < doc->dirty_first_line) doc->dirty_first_line = pos.line;
        mc_proj_invalidate_model_line(&doc->projection, pos.line);
    } else {
        doc->dirty_first_line = 0;
        mc_proj_invalidate_all(&doc->projection);
    }
}

int mc_doc_init(mc_document *doc)
{
    if (doc == NULL) return 0;
    memset(doc, 0, sizeof(*doc));
    mc_pt_init(&doc->tree);
    mc_edit_stack_init(&doc->edit_stack, &doc->tree);
    mc_dec_init(&doc->decorations);
    mc_folding_init(&doc->folding, &doc->decorations);
    mc_cursors_init(&doc->cursors);
    mc_search_init(&doc->search);
    if (!mc_dw_init(&doc->dw)) {
        mc_search_destroy(&doc->search);
        mc_cursors_destroy(&doc->cursors);
        mc_folding_destroy(&doc->folding);
        mc_dec_destroy(&doc->decorations);
        mc_edit_stack_destroy(&doc->edit_stack);
        mc_pt_destroy(&doc->tree);
        return 0;
    }
    mc_proj_init(&doc->projection, &doc->tree, &doc->folding, &doc->dw);
    mc_edit_stack_set_observer(&doc->edit_stack, mc_doc_on_edit_delta, doc);

    doc->tab_size = 4;
    doc->insert_spaces = true;
    doc->tokenize_max_line = MC_DEFAULT_TOKENIZE_MAX_LINE;
    doc->dirty_first_line = 0;
    return 1;
}

void mc_doc_destroy(mc_document *doc)
{
    if (doc == NULL) return;
    mc_proj_destroy(&doc->projection);
    mc_dw_destroy(&doc->dw);
    mc_search_destroy(&doc->search);
    mc_cursors_destroy(&doc->cursors);
    mc_folding_destroy(&doc->folding);
    mc_dec_destroy(&doc->decorations);
    mc_edit_stack_destroy(&doc->edit_stack);
    mc_pt_destroy(&doc->tree);
    memset(doc, 0, sizeof(*doc));
}

int mc_doc_set_text(mc_document *doc, const wchar_t *text, size_t len)
{
    if (doc == NULL) return 0;
    if (!mc_pt_set_text(&doc->tree, text, len)) return 0;
    mc_edit_stack_clear(&doc->edit_stack);
    mc_edit_stack_bump_version(&doc->edit_stack);
    mc_folding_clear(&doc->folding);
    mc_dec_clear(&doc->decorations);
    mc_cursors_set_single(&doc->cursors, (mc_pos_t){ 0, 0 }, (mc_pos_t){ 0, 0 });
    doc->dirty_first_line = 0;
    mc_proj_invalidate_all(&doc->projection);
    if (doc->on_text_changed != NULL) {
        doc->on_text_changed(doc->on_text_changed_ud,
                             mc_edit_stack_version(&doc->edit_stack), 0);
    }
    return 1;
}

size_t mc_doc_get_text(const mc_document *doc, wchar_t *buf, size_t cap)
{
    if (doc == NULL) return 0;
    return mc_pt_get_text(&doc->tree, buf, cap);
}

uint64_t mc_doc_version(const mc_document *doc)
{
    return doc == NULL ? 0 : mc_edit_stack_version(&doc->edit_stack);
}

typedef struct mc_sorted_edit_t {
    size_t start;
    size_t del_len;
    const wchar_t *text;
    size_t ins_len;
} mc_sorted_edit_t;

static int mc_sorted_edit_desc_cmp(const void *a, const void *b)
{
    const mc_sorted_edit_t *ea = (const mc_sorted_edit_t *)a;
    const mc_sorted_edit_t *eb = (const mc_sorted_edit_t *)b;
    if (ea->start != eb->start) return ea->start > eb->start ? -1 : 1;
    return ea->del_len > eb->del_len ? -1 : (ea->del_len < eb->del_len ? 1 : 0);
}

int mc_doc_apply_range_edits(mc_document *doc,
                             const mc_range_t *ranges,
                             const wchar_t *const *texts,
                             const size_t *lens,
                             size_t count,
                             uint64_t timestamp_ms)
{
    mc_sorted_edit_t *sorted;
    mc_edit_op_t *ops;
    size_t i;
    int ok;

    if (doc == NULL || (count > 0 && (ranges == NULL || texts == NULL || lens == NULL)))
        return 0;
    if (count == 0) return 1;

    sorted = (mc_sorted_edit_t *)calloc(count, sizeof(mc_sorted_edit_t));
    ops = (mc_edit_op_t *)calloc(count, sizeof(mc_edit_op_t));
    if (sorted == NULL || ops == NULL) {
        free(sorted);
        free(ops);
        return 0;
    }

    for (i = 0; i < count; ++i) {
        mc_range_t r = mc_range_make(ranges[i].start, ranges[i].end);
        size_t s = 0, e = 0;
        (void)mc_pt_pos_to_offset(&doc->tree, mc_pos_clamp(&doc->tree, r.start), &s);
        (void)mc_pt_pos_to_offset(&doc->tree, mc_pos_clamp(&doc->tree, r.end), &e);
        sorted[i].start = s;
        sorted[i].del_len = e >= s ? (e - s) : 0;
        sorted[i].text = texts[i];
        sorted[i].ins_len = lens[i];
    }
    if (count > 1) {
        qsort(sorted, count, sizeof(mc_sorted_edit_t), mc_sorted_edit_desc_cmp);
    }
    for (i = 0; i < count; ++i) {
        ops[i].offset = sorted[i].start;
        ops[i].delete_len = sorted[i].del_len;
        ops[i].insert_text = sorted[i].text;
        ops[i].insert_len = sorted[i].ins_len;
    }

    doc->dirty_first_line = UINT32_MAX;
    ok = mc_edit_stack_apply_group(&doc->edit_stack, MC_EDIT_OTHER, timestamp_ms,
                                   doc->cursors.items, doc->cursors.count,
                                   ops, count,
                                   doc->cursors.items, doc->cursors.count);
    free(sorted);
    free(ops);
    if (ok) {
        mc_cursors_normalize(&doc->cursors, &doc->tree);
        mc_doc_reveal_cursors(doc);
        if (doc->on_text_changed != NULL) {
            doc->on_text_changed(doc->on_text_changed_ud,
                                 mc_edit_stack_version(&doc->edit_stack),
                                 doc->dirty_first_line == UINT32_MAX ? 0 : doc->dirty_first_line);
        }
    }
    return ok;
}

int mc_doc_undo(mc_document *doc)
{
    mc_selection_t *sels = NULL;
    size_t sel_count = 0;
    if (doc == NULL) return 0;
    doc->dirty_first_line = UINT32_MAX;
    if (!mc_edit_stack_undo(&doc->edit_stack, &sels, &sel_count)) return 0;
    if (sels != NULL && sel_count > 0) {
        (void)mc_cursors_set(&doc->cursors, &doc->tree, sels, sel_count);
    } else {
        mc_cursors_normalize(&doc->cursors, &doc->tree);
    }
    mc_doc_reveal_cursors(doc);
    if (doc->on_text_changed != NULL) {
        doc->on_text_changed(doc->on_text_changed_ud,
                             mc_edit_stack_version(&doc->edit_stack),
                             doc->dirty_first_line == UINT32_MAX ? 0 : doc->dirty_first_line);
    }
    if (doc->on_cursor_changed != NULL) {
        doc->on_cursor_changed(doc->on_cursor_changed_ud);
    }
    return 1;
}

int mc_doc_redo(mc_document *doc)
{
    mc_selection_t *sels = NULL;
    size_t sel_count = 0;
    if (doc == NULL) return 0;
    doc->dirty_first_line = UINT32_MAX;
    if (!mc_edit_stack_redo(&doc->edit_stack, &sels, &sel_count)) return 0;
    if (sels != NULL && sel_count > 0) {
        (void)mc_cursors_set(&doc->cursors, &doc->tree, sels, sel_count);
    } else {
        mc_cursors_normalize(&doc->cursors, &doc->tree);
    }
    mc_doc_reveal_cursors(doc);
    if (doc->on_text_changed != NULL) {
        doc->on_text_changed(doc->on_text_changed_ud,
                             mc_edit_stack_version(&doc->edit_stack),
                             doc->dirty_first_line == UINT32_MAX ? 0 : doc->dirty_first_line);
    }
    if (doc->on_cursor_changed != NULL) {
        doc->on_cursor_changed(doc->on_cursor_changed_ud);
    }
    return 1;
}

void mc_doc_reveal_cursors(mc_document *doc)
{
    size_t i;
    bool revealed = false;
    if (doc == NULL) return;
    for (i = 0; i < doc->cursors.count; ++i) {
        mc_range_t r = mc_selection_range(doc->cursors.items[i]);
        size_t s = 0, e = 0;
        (void)mc_pt_pos_to_offset(&doc->tree, r.start, &s);
        (void)mc_pt_pos_to_offset(&doc->tree, r.end, &e);
        if (mc_folding_reveal_range(&doc->folding, s, e)) {
            revealed = true;
        }
    }
    if (revealed) {
        mc_proj_invalidate_all(&doc->projection);
    }
}

void mc_doc_set_selections(mc_document *doc, const mc_selection_t *sels, size_t count)
{
    if (doc == NULL) return;
    (void)mc_cursors_set(&doc->cursors, &doc->tree, sels, count);
    mc_doc_reveal_cursors(doc);
    if (doc->on_cursor_changed != NULL) {
        doc->on_cursor_changed(doc->on_cursor_changed_ud);
    }
}

void mc_doc_move_up(mc_document *doc, bool extend)
{
    size_t i;
    if (doc == NULL) return;
    (void)mc_proj_ensure_valid(&doc->projection);
    for (i = 0; i < doc->cursors.count; ++i) {
        mc_selection_t *s = &doc->cursors.items[i];
        mc_view_pos_t vp = { 0, 0 };
        mc_pos_t target = s->active;
        if (mc_proj_model_to_view(&doc->projection, s->active, &vp)) {
            uint32_t pref = s->has_preferred_x ? (uint32_t)s->preferred_x : vp.view_col;
            if (!s->has_preferred_x) {
                s->preferred_x = (float)vp.view_col;
                s->has_preferred_x = true;
            }
            if (vp.view_line > 0) {
                vp.view_line -= 1;
                vp.view_col = pref;
                (void)mc_proj_view_to_model(&doc->projection, vp, &target, NULL);
            } else {
                target.line = 0;
                target.col = 0;
            }
        }
        s->active = mc_pos_clamp(&doc->tree, target);
        if (!extend) s->anchor = s->active;
    }
    mc_cursors_normalize(&doc->cursors, &doc->tree);
    mc_doc_reveal_cursors(doc);
    if (doc->on_cursor_changed != NULL) {
        doc->on_cursor_changed(doc->on_cursor_changed_ud);
    }
}

void mc_doc_move_down(mc_document *doc, bool extend)
{
    size_t i;
    uint32_t total_vlines;
    if (doc == NULL) return;
    total_vlines = mc_proj_view_line_count(&doc->projection);
    for (i = 0; i < doc->cursors.count; ++i) {
        mc_selection_t *s = &doc->cursors.items[i];
        mc_view_pos_t vp = { 0, 0 };
        mc_pos_t target = s->active;
        if (mc_proj_model_to_view(&doc->projection, s->active, &vp)) {
            uint32_t pref = s->has_preferred_x ? (uint32_t)s->preferred_x : vp.view_col;
            if (!s->has_preferred_x) {
                s->preferred_x = (float)vp.view_col;
                s->has_preferred_x = true;
            }
            if (vp.view_line + 1 < total_vlines) {
                vp.view_line += 1;
                vp.view_col = pref;
                (void)mc_proj_view_to_model(&doc->projection, vp, &target, NULL);
            } else {
                uint32_t last_l = (uint32_t)(mc_pt_line_count(&doc->tree) - 1);
                target.line = last_l;
                target.col = (uint32_t)mc_pt_line_length(&doc->tree, last_l);
            }
        }
        s->active = mc_pos_clamp(&doc->tree, target);
        if (!extend) s->anchor = s->active;
    }
    mc_cursors_normalize(&doc->cursors, &doc->tree);
    mc_doc_reveal_cursors(doc);
    if (doc->on_cursor_changed != NULL) {
        doc->on_cursor_changed(doc->on_cursor_changed_ud);
    }
}

uint32_t mc_doc_fold_add(mc_document *doc, mc_range_t range,
                         const wchar_t *placeholder, bool collapsed)
{
    uint32_t id;
    if (doc == NULL) return 0;
    id = mc_folding_add(&doc->folding, &doc->tree, range, placeholder, collapsed);
    if (id != 0 && collapsed) {
        mc_proj_invalidate_all(&doc->projection);
    }
    return id;
}

void mc_doc_fold_set_collapsed(mc_document *doc, uint32_t fold_id, bool collapsed)
{
    if (doc == NULL) return;
    if (mc_folding_set_collapsed(&doc->folding, fold_id, collapsed)) {
        mc_proj_invalidate_all(&doc->projection);
    }
}

bool mc_doc_fold_toggle_at(mc_document *doc, mc_pos_t pos)
{
    bool ok;
    if (doc == NULL) return false;
    ok = mc_folding_toggle_at(&doc->folding, &doc->tree, pos);
    if (ok) mc_proj_invalidate_all(&doc->projection);
    return ok;
}

void mc_doc_fold_all(mc_document *doc, bool collapsed)
{
    if (doc == NULL) return;
    mc_folding_fold_all(&doc->folding, collapsed);
    mc_proj_invalidate_all(&doc->projection);
}

void mc_doc_fold_clear(mc_document *doc)
{
    if (doc == NULL) return;
    mc_folding_clear(&doc->folding);
    mc_proj_invalidate_all(&doc->projection);
}
