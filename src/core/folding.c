#include "folding.h"

#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>

static wchar_t *mc_fold_dup_str(const wchar_t *s)
{
    const wchar_t *src = (s != NULL && s[0] != L'\0') ? s : L"\x2026";
    size_t len = wcslen(src);
    wchar_t *copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) return NULL;
    memcpy(copy, src, (len + 1) * sizeof(wchar_t));
    return copy;
}

static size_t mc_folding_find_idx(const mc_folding *f, uint32_t fold_id)
{
    size_t i;
    for (i = 0; i < f->count; ++i) {
        if (f->entries[i].dec_id == fold_id) return i;
    }
    return SIZE_MAX;
}

static void mc_folding_remove_at_idx(mc_folding *f, size_t idx, int remove_dec)
{
    uint32_t dec_id = f->entries[idx].dec_id;
    free(f->entries[idx].placeholder);
    if (idx + 1 < f->count) {
        memmove(&f->entries[idx], &f->entries[idx + 1],
                (f->count - idx - 1) * sizeof(mc_fold_entry_t));
    }
    --f->count;
    if (remove_dec && f->decorations != NULL) {
        (void)mc_dec_remove(f->decorations, dec_id);
    }
}

void mc_folding_init(mc_folding *f, mc_dec_tree *decorations)
{
    if (f == NULL) return;
    memset(f, 0, sizeof(*f));
    f->decorations = decorations;
}

void mc_folding_destroy(mc_folding *f)
{
    if (f == NULL) return;
    mc_folding_clear(f);
    free(f->entries);
    memset(f, 0, sizeof(*f));
}

void mc_folding_clear(mc_folding *f)
{
    size_t i;
    if (f == NULL) return;
    for (i = 0; i < f->count; ++i) {
        free(f->entries[i].placeholder);
    }
    f->count = 0;
    if (f->decorations != NULL) {
        mc_dec_remove_by_owner(f->decorations, MC_OWNER_FOLDING);
    }
}

void mc_folding_set_toggled_cb(mc_folding *f, mc_fold_toggled_cb cb, void *userdata)
{
    if (f == NULL) return;
    f->on_toggled = cb;
    f->on_toggled_ud = userdata;
}

static int mc_ranges_partially_overlap(size_t a0, size_t a1, size_t b0, size_t b1)
{
    /* Disjoint */
    if (a1 <= b0 || b1 <= a0) return 0;
    /* Exact duplicate -> reject */
    if (a0 == b0 && a1 == b1) return 1;
    /* A strictly contains or shares one boundary with B (nested) */
    if (a0 <= b0 && b1 <= a1) return 0;
    /* B contains A (nested) */
    if (b0 <= a0 && a1 <= b1) return 0;
    /* Otherwise partial crossing overlap -> reject */
    return 1;
}

uint32_t mc_folding_add_offsets(mc_folding *f, size_t start_offset, size_t end_offset,
                                const wchar_t *placeholder, bool collapsed)
{
    mc_dec_snapshot_t *overlapping = NULL;
    size_t overlap_count;
    size_t i;
    mc_dec_spec_t spec;
    uint32_t dec_id;
    wchar_t *ph;

    if (f == NULL || f->decorations == NULL || start_offset >= end_offset)
        return 0;

    overlap_count = mc_dec_query_owner(f->decorations, MC_OWNER_FOLDING,
                                       start_offset, end_offset, NULL, 0);
    if (overlap_count > 0) {
        overlapping = (mc_dec_snapshot_t *)malloc(overlap_count * sizeof(mc_dec_snapshot_t));
        if (overlapping == NULL) return 0;
        mc_dec_query_owner(f->decorations, MC_OWNER_FOLDING,
                           start_offset, end_offset, overlapping, overlap_count);
        for (i = 0; i < overlap_count; ++i) {
            if (mc_ranges_partially_overlap(start_offset, end_offset,
                                            overlapping[i].start,
                                            overlapping[i].end)) {
                free(overlapping);
                return 0;
            }
        }
        free(overlapping);
    }

    if (!mc_vec_reserve((void **)&f->entries, &f->capacity,
                        sizeof(mc_fold_entry_t), f->count + 1)) {
        return 0;
    }

    ph = mc_fold_dup_str(placeholder);
    if (ph == NULL) return 0;

    memset(&spec, 0, sizeof(spec));
    spec.start = start_offset;
    spec.end = end_offset;
    spec.stickiness = MC_STICKY_NEVER_GROWS;
    spec.collapse_on_delete = true;
    dec_id = mc_dec_add(f->decorations, MC_OWNER_FOLDING, &spec);
    if (dec_id == 0) {
        free(ph);
        return 0;
    }

    f->entries[f->count].dec_id = dec_id;
    f->entries[f->count].collapsed = collapsed;
    f->entries[f->count].placeholder = ph;
    ++f->count;
    return dec_id;
}

uint32_t mc_folding_add(mc_folding *f, const mc_piece_tree *tree,
                        mc_range_t range, const wchar_t *placeholder,
                        bool collapsed)
{
    size_t s_off = 0, e_off = 0;
    mc_range_t norm = mc_range_make(range.start, range.end);
    if (f == NULL || tree == NULL) return 0;
    if (!mc_pt_pos_to_offset(tree, norm.start, &s_off)) return 0;
    if (!mc_pt_pos_to_offset(tree, norm.end, &e_off)) return 0;
    return mc_folding_add_offsets(f, s_off, e_off, placeholder, collapsed);
}

int mc_folding_remove(mc_folding *f, uint32_t fold_id)
{
    size_t idx;
    if (f == NULL || fold_id == 0) return 0;
    idx = mc_folding_find_idx(f, fold_id);
    if (idx == SIZE_MAX) return 0;
    mc_folding_remove_at_idx(f, idx, 1);
    return 1;
}

int mc_folding_set_collapsed(mc_folding *f, uint32_t fold_id, bool collapsed)
{
    size_t idx;
    if (f == NULL || fold_id == 0) return 0;
    idx = mc_folding_find_idx(f, fold_id);
    if (idx == SIZE_MAX) return 0;
    if (f->entries[idx].collapsed != collapsed) {
        f->entries[idx].collapsed = collapsed;
        if (f->on_toggled != NULL) {
            f->on_toggled(f->on_toggled_ud, fold_id, collapsed);
        }
    }
    return 1;
}

bool mc_folding_toggle_at(mc_folding *f, const mc_piece_tree *tree, mc_pos_t pos)
{
    size_t offset = 0;
    size_t i;
    size_t best_idx = SIZE_MAX;
    size_t best_span = SIZE_MAX;

    if (f == NULL || tree == NULL || f->decorations == NULL) return false;
    if (!mc_pt_pos_to_offset(tree, pos, &offset)) return false;

    /* Find innermost fold containing offset (inclusive of boundary) */
    for (i = 0; i < f->count; ++i) {
        mc_dec_snapshot_t snap;
        if (!mc_dec_get(f->decorations, f->entries[i].dec_id, &snap)) continue;
        if (snap.start <= offset && offset <= snap.end) {
            size_t span = snap.end - snap.start;
            if (span < best_span) {
                best_span = span;
                best_idx = i;
            }
        }
    }
    /* Fallback: if none contains exact pos, find first fold starting on pos.line */
    if (best_idx == SIZE_MAX) {
        for (i = 0; i < f->count; ++i) {
            mc_dec_snapshot_t snap;
            mc_pos_t sp;
            if (!mc_dec_get(f->decorations, f->entries[i].dec_id, &snap)) continue;
            if (mc_pt_offset_to_pos(tree, snap.start, &sp) && sp.line == pos.line) {
                best_idx = i;
                break;
            }
        }
    }
    if (best_idx == SIZE_MAX) return false;

    f->entries[best_idx].collapsed = !f->entries[best_idx].collapsed;
    if (f->on_toggled != NULL) {
        f->on_toggled(f->on_toggled_ud, f->entries[best_idx].dec_id,
                      f->entries[best_idx].collapsed);
    }
    return true;
}

void mc_folding_fold_all(mc_folding *f, bool collapsed)
{
    size_t i;
    if (f == NULL) return;
    for (i = 0; i < f->count; ++i) {
        if (f->entries[i].collapsed != collapsed) {
            f->entries[i].collapsed = collapsed;
            if (f->on_toggled != NULL) {
                f->on_toggled(f->on_toggled_ud, f->entries[i].dec_id, collapsed);
            }
        }
    }
}

bool mc_folding_reveal_offset(mc_folding *f, size_t offset)
{
    size_t i;
    bool changed = false;
    if (f == NULL || f->decorations == NULL) return false;
    for (i = 0; i < f->count; ++i) {
        mc_dec_snapshot_t snap;
        if (!f->entries[i].collapsed) continue;
        if (!mc_dec_get(f->decorations, f->entries[i].dec_id, &snap)) continue;
        if (snap.start < offset && offset < snap.end) {
            f->entries[i].collapsed = false;
            changed = true;
            if (f->on_toggled != NULL) {
                f->on_toggled(f->on_toggled_ud, f->entries[i].dec_id, false);
            }
        }
    }
    return changed;
}

bool mc_folding_reveal_range(mc_folding *f, size_t start_offset, size_t end_offset)
{
    size_t i;
    bool changed = false;
    if (f == NULL || f->decorations == NULL) return false;
    if (end_offset < start_offset) {
        size_t tmp = start_offset;
        start_offset = end_offset;
        end_offset = tmp;
    }
    for (i = 0; i < f->count; ++i) {
        mc_dec_snapshot_t snap;
        bool inside = false;
        if (!f->entries[i].collapsed) continue;
        if (!mc_dec_get(f->decorations, f->entries[i].dec_id, &snap)) continue;
        if (snap.start < start_offset && start_offset < snap.end) inside = true;
        if (snap.start < end_offset && end_offset < snap.end) inside = true;
        if (start_offset < end_offset &&
            start_offset <= snap.start && snap.end <= end_offset) {
            /* Selection encompasses the whole fold: reveal when editing/interacting */
        }
        if (inside) {
            f->entries[i].collapsed = false;
            changed = true;
            if (f->on_toggled != NULL) {
                f->on_toggled(f->on_toggled_ud, f->entries[i].dec_id, false);
            }
        }
    }
    return changed;
}

void mc_folding_will_replace(mc_folding *f, size_t offset,
                             size_t deleted_len, size_t inserted_len)
{
    size_t i;
    size_t del_end = offset + deleted_len;
    if (f == NULL || f->decorations == NULL) return;
    if (deleted_len == 0 && inserted_len == 0) return;

    i = 0;
    while (i < f->count) {
        mc_dec_snapshot_t snap;
        if (!mc_dec_get(f->decorations, f->entries[i].dec_id, &snap)) {
            mc_folding_remove_at_idx(f, i, 0);
            continue;
        }
        /* Boundary deletion check: first char is snap.start, last char is snap.end - 1 */
        if (deleted_len > 0 && snap.end > snap.start) {
            bool start_deleted = (offset <= snap.start && snap.start < del_end);
            bool end_deleted = (offset <= snap.end - 1 && snap.end - 1 < del_end);
            if (start_deleted || end_deleted) {
                mc_folding_remove_at_idx(f, i, 1);
                continue;
            }
        }
        /* Edit inside a collapsed fold -> auto-unfold */
        if (f->entries[i].collapsed) {
            bool inside_insert = (inserted_len > 0 &&
                                  snap.start < offset && offset < snap.end);
            bool inside_delete = (deleted_len > 0 &&
                                  offset < snap.end && del_end > snap.start);
            if (inside_insert || inside_delete) {
                f->entries[i].collapsed = false;
                if (f->on_toggled != NULL) {
                    f->on_toggled(f->on_toggled_ud, f->entries[i].dec_id, false);
                }
            }
        }
        ++i;
    }
}

void mc_folding_did_replace(mc_folding *f)
{
    size_t i = 0;
    if (f == NULL || f->decorations == NULL) return;
    while (i < f->count) {
        mc_dec_snapshot_t snap;
        if (!mc_dec_get(f->decorations, f->entries[i].dec_id, &snap) ||
            snap.start >= snap.end) {
            mc_folding_remove_at_idx(f, i, 1);
            continue;
        }
        ++i;
    }
}

size_t mc_folding_count(const mc_folding *f)
{
    return f == NULL ? 0 : f->count;
}

int mc_folding_get(mc_folding *f, const mc_piece_tree *tree,
                   uint32_t fold_id, mc_fold_info_t *out)
{
    size_t idx;
    mc_dec_snapshot_t snap;
    if (f == NULL || out == NULL || f->decorations == NULL) return 0;
    idx = mc_folding_find_idx(f, fold_id);
    if (idx == SIZE_MAX) return 0;
    if (!mc_dec_get(f->decorations, fold_id, &snap)) return 0;
    out->id = fold_id;
    out->start_offset = snap.start;
    out->end_offset = snap.end;
    if (tree != NULL) {
        (void)mc_pt_offset_to_pos(tree, snap.start, &out->range.start);
        (void)mc_pt_offset_to_pos(tree, snap.end, &out->range.end);
    } else {
        memset(&out->range, 0, sizeof(out->range));
    }
    out->collapsed = f->entries[idx].collapsed;
    out->placeholder = f->entries[idx].placeholder;
    return 1;
}

static int mc_fold_info_cmp(const void *a, const void *b)
{
    const mc_fold_info_t *fa = (const mc_fold_info_t *)a;
    const mc_fold_info_t *fb = (const mc_fold_info_t *)b;
    if (fa->start_offset != fb->start_offset)
        return fa->start_offset < fb->start_offset ? -1 : 1;
    if (fa->end_offset != fb->end_offset)
        return fa->end_offset > fb->end_offset ? -1 : 1; /* outer fold first */
    return fa->id < fb->id ? -1 : (fa->id > fb->id ? 1 : 0);
}

size_t mc_folding_list(mc_folding *f, const mc_piece_tree *tree,
                       mc_fold_info_t *out, size_t cap)
{
    size_t i;
    size_t n = 0;
    if (f == NULL || f->decorations == NULL) return 0;
    for (i = 0; i < f->count; ++i) {
        mc_fold_info_t info;
        if (!mc_folding_get(f, tree, f->entries[i].dec_id, &info)) continue;
        if (out != NULL && n < cap) out[n] = info;
        ++n;
    }
    if (out != NULL && n > 1) {
        size_t sort_n = n < cap ? n : cap;
        qsort(out, sort_n, sizeof(mc_fold_info_t), mc_fold_info_cmp);
    }
    return n;
}

size_t mc_folding_list_top_collapsed(mc_folding *f, const mc_piece_tree *tree,
                                     mc_fold_info_t *out, size_t cap)
{
    mc_fold_info_t *all;
    size_t total;
    size_t i;
    size_t top_n = 0;
    size_t cur_end = 0;
    bool has_cur = false;

    if (f == NULL || f->count == 0) return 0;
    all = (mc_fold_info_t *)malloc(f->count * sizeof(mc_fold_info_t));
    if (all == NULL) return 0;

    total = 0;
    for (i = 0; i < f->count; ++i) {
        mc_fold_info_t info;
        if (!f->entries[i].collapsed) continue;
        if (!mc_folding_get(f, tree, f->entries[i].dec_id, &info)) continue;
        if (info.start_offset >= info.end_offset) continue;
        all[total++] = info;
    }
    if (total > 1) {
        qsort(all, total, sizeof(mc_fold_info_t), mc_fold_info_cmp);
    }
    for (i = 0; i < total; ++i) {
        if (has_cur && all[i].end_offset <= cur_end) {
            continue; /* nested inside an outer collapsed fold */
        }
        if (out != NULL && top_n < cap) {
            out[top_n] = all[i];
        }
        ++top_n;
        cur_end = all[i].end_offset;
        has_cur = true;
    }
    free(all);
    return top_n;
}
