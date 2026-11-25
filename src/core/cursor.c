#include "cursor.h"

#include "../base/utf.h"
#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static int mc_sel_cmp(const void *a, const void *b)
{
    const mc_selection_t *sa = (const mc_selection_t *)a;
    const mc_selection_t *sb = (const mc_selection_t *)b;
    mc_range_t ra = mc_selection_range(*sa);
    mc_range_t rb = mc_selection_range(*sb);
    int c = mc_pos_cmp(ra.start, rb.start);
    if (c != 0) return c;
    return mc_pos_cmp(ra.end, rb.end);
}

bool mc_is_word_char(wchar_t ch)
{
    return ch == L'_' || iswalnum((wint_t)ch) != 0;
}

mc_pos_t mc_pos_clamp(const mc_piece_tree *tree, mc_pos_t pos)
{
    size_t lc;
    size_t ll;
    if (tree == NULL) return pos;
    lc = mc_pt_line_count(tree);
    if (lc == 0) {
        pos.line = 0;
        pos.col = 0;
        return pos;
    }
    if ((size_t)pos.line >= lc) pos.line = (uint32_t)(lc - 1);
    ll = mc_pt_line_length(tree, pos.line);
    if ((size_t)pos.col > ll) pos.col = (uint32_t)ll;
    if (pos.col > 0 && pos.col < ll) {
        mc_wbuf line;
        mc_wbuf_init(&line);
        if (mc_pt_get_line(tree, pos.line, &line)) {
            if (!mc_utf16_is_boundary(line.data, line.length, pos.col)) {
                pos.col = (uint32_t)mc_utf16_prev_boundary(line.data, line.length, pos.col);
            }
        }
        mc_wbuf_destroy(&line);
    }
    return pos;
}

void mc_cursors_init(mc_cursor_set *cs)
{
    if (cs == NULL) return;
    memset(cs, 0, sizeof(*cs));
    mc_cursors_set_single(cs, (mc_pos_t){ 0, 0 }, (mc_pos_t){ 0, 0 });
}

void mc_cursors_destroy(mc_cursor_set *cs)
{
    if (cs == NULL) return;
    free(cs->items);
    memset(cs, 0, sizeof(*cs));
}

void mc_cursors_set_single(mc_cursor_set *cs, mc_pos_t anchor, mc_pos_t active)
{
    if (cs == NULL) return;
    if (!mc_vec_reserve((void **)&cs->items, &cs->capacity,
                        sizeof(mc_selection_t), 1)) return;
    cs->items[0].anchor = anchor;
    cs->items[0].active = active;
    cs->items[0].preferred_x = 0.0f;
    cs->items[0].has_preferred_x = false;
    cs->count = 1;
    cs->primary_index = 0;
}

void mc_cursors_normalize(mc_cursor_set *cs, const mc_piece_tree *tree)
{
    size_t i;
    size_t out_n = 0;
    mc_selection_t primary_val;

    if (cs == NULL) return;
    if (cs->count == 0) {
        mc_cursors_set_single(cs, (mc_pos_t){ 0, 0 }, (mc_pos_t){ 0, 0 });
        return;
    }
    if (cs->primary_index >= cs->count) cs->primary_index = 0;
    primary_val = cs->items[cs->primary_index];

    if (tree != NULL) {
        for (i = 0; i < cs->count; ++i) {
            cs->items[i].anchor = mc_pos_clamp(tree, cs->items[i].anchor);
            cs->items[i].active = mc_pos_clamp(tree, cs->items[i].active);
        }
        primary_val.anchor = mc_pos_clamp(tree, primary_val.anchor);
        primary_val.active = mc_pos_clamp(tree, primary_val.active);
    }

    if (cs->count > 1) {
        qsort(cs->items, cs->count, sizeof(mc_selection_t), mc_sel_cmp);
    }

    for (i = 0; i < cs->count; ++i) {
        mc_selection_t cur = cs->items[i];
        mc_range_t rc = mc_selection_range(cur);
        if (out_n == 0) {
            cs->items[out_n++] = cur;
            continue;
        }
        {
            mc_selection_t *prev = &cs->items[out_n - 1];
            mc_range_t rp = mc_selection_range(*prev);
            bool overlap = false;
            if (mc_range_is_empty(rp) && mc_range_is_empty(rc)) {
                overlap = (mc_pos_cmp(rp.start, rc.start) == 0);
            } else if (mc_range_is_empty(rp)) {
                overlap = (mc_pos_cmp(rp.start, rc.start) >= 0 &&
                           mc_pos_cmp(rp.start, rc.end) <= 0);
            } else if (mc_range_is_empty(rc)) {
                overlap = (mc_pos_cmp(rc.start, rp.start) >= 0 &&
                           mc_pos_cmp(rc.start, rp.end) <= 0);
            } else {
                overlap = (mc_pos_cmp(rc.start, rp.end) < 0 ||
                           (mc_pos_cmp(rc.start, rp.start) == 0 &&
                            mc_pos_cmp(rc.end, rp.end) == 0));
            }
            if (overlap) {
                mc_pos_t ns = mc_pos_cmp(rp.start, rc.start) <= 0 ? rp.start : rc.start;
                mc_pos_t ne = mc_pos_cmp(rp.end, rc.end) >= 0 ? rp.end : rc.end;
                bool reversed = mc_pos_cmp(prev->active, prev->anchor) < 0;
                prev->anchor = reversed ? ne : ns;
                prev->active = reversed ? ns : ne;
                prev->has_preferred_x = false;
            } else {
                cs->items[out_n++] = cur;
            }
        }
    }
    cs->count = out_n;
    cs->primary_index = 0;
    {
        mc_range_t pr = mc_selection_range(primary_val);
        for (i = 0; i < cs->count; ++i) {
            mc_range_t ri = mc_selection_range(cs->items[i]);
            if (mc_pos_cmp(ri.start, pr.start) <= 0 && mc_pos_cmp(pr.end, ri.end) <= 0) {
                cs->primary_index = i;
                break;
            }
        }
    }
}

int mc_cursors_set(mc_cursor_set *cs, const mc_piece_tree *tree,
                   const mc_selection_t *sels, size_t count)
{
    if (cs == NULL) return 0;
    if (sels == NULL || count == 0) {
        mc_cursors_set_single(cs, (mc_pos_t){ 0, 0 }, (mc_pos_t){ 0, 0 });
        return 1;
    }
    if (!mc_vec_reserve((void **)&cs->items, &cs->capacity,
                        sizeof(mc_selection_t), count)) return 0;
    memcpy(cs->items, sels, count * sizeof(mc_selection_t));
    cs->count = count;
    cs->primary_index = 0;
    mc_cursors_normalize(cs, tree);
    return 1;
}

int mc_cursors_add(mc_cursor_set *cs, const mc_piece_tree *tree, mc_selection_t sel)
{
    if (cs == NULL) return 0;
    if (!mc_vec_reserve((void **)&cs->items, &cs->capacity,
                        sizeof(mc_selection_t), cs->count + 1)) return 0;
    cs->items[cs->count] = sel;
    cs->primary_index = cs->count;
    ++cs->count;
    mc_cursors_normalize(cs, tree);
    return 1;
}

void mc_cursors_collapse_to_primary(mc_cursor_set *cs)
{
    mc_selection_t p;
    if (cs == NULL || cs->count <= 1) return;
    p = mc_cursors_primary(cs);
    cs->items[0] = p;
    cs->count = 1;
    cs->primary_index = 0;
}

void mc_cursors_select_all(mc_cursor_set *cs, const mc_piece_tree *tree)
{
    mc_pos_t end_pos = { 0, 0 };
    if (cs == NULL || tree == NULL) return;
    (void)mc_pt_offset_to_pos(tree, mc_pt_length(tree), &end_pos);
    mc_cursors_set_single(cs, (mc_pos_t){ 0, 0 }, end_pos);
}

mc_selection_t mc_cursors_primary(const mc_cursor_set *cs)
{
    mc_selection_t empty = { { 0, 0 }, { 0, 0 }, 0.0f, false };
    if (cs == NULL || cs->count == 0) return empty;
    return cs->items[cs->primary_index < cs->count ? cs->primary_index : 0];
}

mc_pos_t mc_pos_move_left(const mc_piece_tree *tree, mc_pos_t pos)
{
    mc_wbuf line;
    if (tree == NULL) return pos;
    pos = mc_pos_clamp(tree, pos);
    if (pos.col > 0) {
        mc_wbuf_init(&line);
        if (mc_pt_get_line(tree, pos.line, &line)) {
            pos.col = (uint32_t)mc_utf16_prev_grapheme(line.data, line.length, pos.col);
        } else {
            --pos.col;
        }
        mc_wbuf_destroy(&line);
        return pos;
    }
    if (pos.line > 0) {
        --pos.line;
        pos.col = (uint32_t)mc_pt_line_length(tree, pos.line);
    }
    return pos;
}

mc_pos_t mc_pos_move_right(const mc_piece_tree *tree, mc_pos_t pos)
{
    size_t ll;
    size_t lc;
    mc_wbuf line;
    if (tree == NULL) return pos;
    pos = mc_pos_clamp(tree, pos);
    ll = mc_pt_line_length(tree, pos.line);
    if (pos.col < ll) {
        mc_wbuf_init(&line);
        if (mc_pt_get_line(tree, pos.line, &line)) {
            pos.col = (uint32_t)mc_utf16_next_grapheme(line.data, line.length, pos.col);
        } else {
            ++pos.col;
        }
        mc_wbuf_destroy(&line);
        return pos;
    }
    lc = mc_pt_line_count(tree);
    if ((size_t)pos.line + 1 < lc) {
        ++pos.line;
        pos.col = 0;
    }
    return pos;
}

static int mc_char_class(wchar_t ch)
{
    if (ch == L' ' || ch == L'\t') return 0;
    if (mc_is_word_char(ch)) return 1;
    return 2;
}

mc_pos_t mc_pos_move_word_left(const mc_piece_tree *tree, mc_pos_t pos)
{
    mc_wbuf line;
    size_t col;
    int cls;
    if (tree == NULL) return pos;
    pos = mc_pos_clamp(tree, pos);
    if (pos.col == 0) {
        if (pos.line > 0) {
            --pos.line;
            pos.col = (uint32_t)mc_pt_line_length(tree, pos.line);
        }
        return pos;
    }
    mc_wbuf_init(&line);
    if (!mc_pt_get_line(tree, pos.line, &line)) {
        mc_wbuf_destroy(&line);
        return pos;
    }
    col = pos.col;
    while (col > 0 && mc_char_class(line.data[col - 1]) == 0) {
        col = mc_utf16_prev_boundary(line.data, line.length, col);
    }
    if (col > 0) {
        cls = mc_char_class(line.data[col - 1]);
        while (col > 0 && mc_char_class(line.data[col - 1]) == cls) {
            col = mc_utf16_prev_boundary(line.data, line.length, col);
        }
    }
    pos.col = (uint32_t)col;
    mc_wbuf_destroy(&line);
    return pos;
}

mc_pos_t mc_pos_move_word_right(const mc_piece_tree *tree, mc_pos_t pos)
{
    mc_wbuf line;
    size_t col;
    int cls;
    if (tree == NULL) return pos;
    pos = mc_pos_clamp(tree, pos);
    mc_wbuf_init(&line);
    if (!mc_pt_get_line(tree, pos.line, &line)) {
        mc_wbuf_destroy(&line);
        return pos;
    }
    if (pos.col >= line.length) {
        mc_wbuf_destroy(&line);
        if ((size_t)pos.line + 1 < mc_pt_line_count(tree)) {
            ++pos.line;
            pos.col = 0;
        }
        return pos;
    }
    col = pos.col;
    cls = mc_char_class(line.data[col]);
    while (col < line.length && mc_char_class(line.data[col]) == cls) {
        col = mc_utf16_next_boundary(line.data, line.length, col);
    }
    while (col < line.length && mc_char_class(line.data[col]) == 0) {
        col = mc_utf16_next_boundary(line.data, line.length, col);
    }
    pos.col = (uint32_t)col;
    mc_wbuf_destroy(&line);
    return pos;
}

mc_range_t mc_word_range_at(const mc_piece_tree *tree, mc_pos_t pos)
{
    mc_range_t r;
    mc_wbuf line;
    size_t s, e;
    int cls;
    pos = mc_pos_clamp(tree, pos);
    r.start = pos;
    r.end = pos;
    if (tree == NULL) return r;
    mc_wbuf_init(&line);
    if (!mc_pt_get_line(tree, pos.line, &line) || line.length == 0) {
        mc_wbuf_destroy(&line);
        return r;
    }
    s = pos.col;
    if (s == line.length && s > 0) s = mc_utf16_prev_boundary(line.data, line.length, s);
    if (s < line.length && !mc_is_word_char(line.data[s]) && s > 0 &&
        mc_is_word_char(line.data[s - 1])) {
        s = mc_utf16_prev_boundary(line.data, line.length, s);
    }
    cls = mc_char_class(line.data[s]);
    e = mc_utf16_next_boundary(line.data, line.length, s);
    while (s > 0 && mc_char_class(line.data[s - 1]) == cls) {
        s = mc_utf16_prev_boundary(line.data, line.length, s);
    }
    while (e < line.length && mc_char_class(line.data[e]) == cls) {
        e = mc_utf16_next_boundary(line.data, line.length, e);
    }
    r.start.line = pos.line;
    r.start.col = (uint32_t)s;
    r.end.line = pos.line;
    r.end.col = (uint32_t)e;
    mc_wbuf_destroy(&line);
    return r;
}

void mc_cursors_move_left(mc_cursor_set *cs, const mc_piece_tree *tree,
                          bool extend, bool by_word)
{
    size_t i;
    if (cs == NULL || tree == NULL) return;
    for (i = 0; i < cs->count; ++i) {
        mc_selection_t *s = &cs->items[i];
        if (!extend && mc_pos_cmp(s->anchor, s->active) != 0) {
            mc_range_t r = mc_selection_range(*s);
            s->anchor = r.start;
            s->active = r.start;
        } else {
            s->active = by_word ? mc_pos_move_word_left(tree, s->active)
                                : mc_pos_move_left(tree, s->active);
            if (!extend) s->anchor = s->active;
        }
        s->has_preferred_x = false;
    }
    mc_cursors_normalize(cs, tree);
}

void mc_cursors_move_right(mc_cursor_set *cs, const mc_piece_tree *tree,
                           bool extend, bool by_word)
{
    size_t i;
    if (cs == NULL || tree == NULL) return;
    for (i = 0; i < cs->count; ++i) {
        mc_selection_t *s = &cs->items[i];
        if (!extend && mc_pos_cmp(s->anchor, s->active) != 0) {
            mc_range_t r = mc_selection_range(*s);
            s->anchor = r.end;
            s->active = r.end;
        } else {
            s->active = by_word ? mc_pos_move_word_right(tree, s->active)
                                : mc_pos_move_right(tree, s->active);
            if (!extend) s->anchor = s->active;
        }
        s->has_preferred_x = false;
    }
    mc_cursors_normalize(cs, tree);
}

void mc_cursors_move_up(mc_cursor_set *cs, const mc_piece_tree *tree, bool extend)
{
    size_t i;
    if (cs == NULL || tree == NULL) return;
    for (i = 0; i < cs->count; ++i) {
        mc_selection_t *s = &cs->items[i];
        uint32_t pref_col = s->has_preferred_x ? (uint32_t)s->preferred_x : s->active.col;
        if (!s->has_preferred_x) {
            s->preferred_x = (float)s->active.col;
            s->has_preferred_x = true;
        }
        if (s->active.line > 0) {
            --s->active.line;
            s->active.col = pref_col;
            s->active = mc_pos_clamp(tree, s->active);
        } else {
            s->active.col = 0;
        }
        if (!extend) s->anchor = s->active;
    }
    mc_cursors_normalize(cs, tree);
}

void mc_cursors_move_down(mc_cursor_set *cs, const mc_piece_tree *tree, bool extend)
{
    size_t i;
    size_t lc;
    if (cs == NULL || tree == NULL) return;
    lc = mc_pt_line_count(tree);
    for (i = 0; i < cs->count; ++i) {
        mc_selection_t *s = &cs->items[i];
        uint32_t pref_col = s->has_preferred_x ? (uint32_t)s->preferred_x : s->active.col;
        if (!s->has_preferred_x) {
            s->preferred_x = (float)s->active.col;
            s->has_preferred_x = true;
        }
        if ((size_t)s->active.line + 1 < lc) {
            ++s->active.line;
            s->active.col = pref_col;
            s->active = mc_pos_clamp(tree, s->active);
        } else {
            s->active.col = (uint32_t)mc_pt_line_length(tree, s->active.line);
        }
        if (!extend) s->anchor = s->active;
    }
    mc_cursors_normalize(cs, tree);
}

void mc_cursors_move_home(mc_cursor_set *cs, const mc_piece_tree *tree,
                          bool extend, bool smart)
{
    size_t i;
    mc_wbuf line;
    if (cs == NULL || tree == NULL) return;
    mc_wbuf_init(&line);
    for (i = 0; i < cs->count; ++i) {
        mc_selection_t *s = &cs->items[i];
        uint32_t target = 0;
        if (smart && mc_pt_get_line(tree, s->active.line, &line)) {
            uint32_t first_non_ws = 0;
            while (first_non_ws < line.length &&
                   (line.data[first_non_ws] == L' ' || line.data[first_non_ws] == L'\t')) {
                ++first_non_ws;
            }
            target = (s->active.col == first_non_ws) ? 0 : first_non_ws;
        }
        s->active.col = target;
        if (!extend) s->anchor = s->active;
        s->has_preferred_x = false;
    }
    mc_wbuf_destroy(&line);
    mc_cursors_normalize(cs, tree);
}

void mc_cursors_move_end(mc_cursor_set *cs, const mc_piece_tree *tree, bool extend)
{
    size_t i;
    if (cs == NULL || tree == NULL) return;
    for (i = 0; i < cs->count; ++i) {
        mc_selection_t *s = &cs->items[i];
        s->active.col = (uint32_t)mc_pt_line_length(tree, s->active.line);
        if (!extend) s->anchor = s->active;
        s->has_preferred_x = false;
    }
    mc_cursors_normalize(cs, tree);
}

bool mc_cursors_add_next_occurrence(mc_cursor_set *cs, const mc_piece_tree *tree)
{
    mc_selection_t primary;
    mc_range_t pr;
    size_t s_off = 0, e_off = 0;
    mc_wbuf needle, hay;
    size_t total;
    size_t search_from;
    size_t found_off = SIZE_MAX;
    size_t i;

    if (cs == NULL || tree == NULL || cs->count == 0) return false;
    primary = mc_cursors_primary(cs);
    pr = mc_selection_range(primary);
    if (mc_range_is_empty(pr)) {
        mc_range_t wr = mc_word_range_at(tree, primary.active);
        if (mc_range_is_empty(wr)) return false;
        cs->items[cs->primary_index].anchor = wr.start;
        cs->items[cs->primary_index].active = wr.end;
        mc_cursors_normalize(cs, tree);
        return true;
    }

    if (!mc_pt_pos_to_offset(tree, pr.start, &s_off) ||
        !mc_pt_pos_to_offset(tree, pr.end, &e_off) ||
        e_off <= s_off) {
        return false;
    }

    mc_wbuf_init(&needle);
    mc_wbuf_init(&hay);
    if (!mc_pt_get_range(tree, s_off, e_off - s_off, &needle)) {
        mc_wbuf_destroy(&needle);
        mc_wbuf_destroy(&hay);
        return false;
    }
    total = mc_pt_length(tree);
    if (!mc_pt_get_range(tree, 0, total, &hay)) {
        mc_wbuf_destroy(&needle);
        mc_wbuf_destroy(&hay);
        return false;
    }

    /* Search starting right after last cursor, wrapping around */
    {
        mc_range_t last_r = mc_selection_range(cs->items[cs->count - 1]);
        (void)mc_pt_pos_to_offset(tree, last_r.end, &search_from);
    }

    for (i = 0; i < 2 && found_off == SIZE_MAX; ++i) {
        size_t pos = (i == 0) ? search_from : 0;
        size_t limit = (i == 0) ? total : search_from;
        while (pos + needle.length <= limit) {
            if (memcmp(hay.data + pos, needle.data, needle.length * sizeof(wchar_t)) == 0) {
                /* Skip if already selected */
                bool dup = false;
                size_t k;
                for (k = 0; k < cs->count; ++k) {
                    size_t ks = 0, ke = 0;
                    mc_range_t rk = mc_selection_range(cs->items[k]);
                    (void)mc_pt_pos_to_offset(tree, rk.start, &ks);
                    (void)mc_pt_pos_to_offset(tree, rk.end, &ke);
                    if (ks == pos && ke == pos + needle.length) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    found_off = pos;
                    break;
                }
                pos += needle.length;
            } else {
                ++pos;
            }
        }
    }

    mc_wbuf_destroy(&needle);
    mc_wbuf_destroy(&hay);

    if (found_off == SIZE_MAX) return false;
    {
        mc_selection_t added;
        memset(&added, 0, sizeof(added));
        (void)mc_pt_offset_to_pos(tree, found_off, &added.anchor);
        (void)mc_pt_offset_to_pos(tree, found_off + (e_off - s_off), &added.active);
        return mc_cursors_add(cs, tree, added) != 0;
    }
}

static size_t mc_normalized_len(const wchar_t *text, size_t len)
{
    size_t i, n = 0;
    for (i = 0; i < len; ++i) {
        if (text[i] == L'\r' && i + 1 < len && text[i + 1] == L'\n') ++i;
        ++n;
    }
    return n;
}

static int mc_apply_per_cursor_edits(mc_cursor_set *cs, mc_edit_stack *stack,
                                     const size_t *del_starts, const size_t *del_lens,
                                     const wchar_t *const *ins_texts,
                                     const size_t *ins_lens,
                                     mc_edit_kind_t kind, uint64_t timestamp_ms)
{
    size_t n = cs->count;
    mc_edit_op_t *ops;
    mc_selection_t *before;
    mc_selection_t *after;
    size_t *after_offsets;
    size_t i;
    int64_t cumulative = 0;
    int ok;

    if (n == 0) return 1;
    ops = (mc_edit_op_t *)calloc(n, sizeof(mc_edit_op_t));
    before = (mc_selection_t *)malloc(n * sizeof(mc_selection_t));
    after = (mc_selection_t *)calloc(n, sizeof(mc_selection_t));
    after_offsets = (size_t *)calloc(n, sizeof(size_t));
    if (ops == NULL || before == NULL || after == NULL || after_offsets == NULL) {
        free(ops);
        free(before);
        free(after);
        free(after_offsets);
        return 0;
    }
    memcpy(before, cs->items, n * sizeof(mc_selection_t));

    /* Compute post-edit offsets in ascending order */
    for (i = 0; i < n; ++i) {
        size_t norm_ins = mc_normalized_len(ins_texts[i], ins_lens[i]);
        int64_t new_off = (int64_t)del_starts[i] + cumulative + (int64_t)norm_ins;
        after_offsets[i] = new_off < 0 ? 0 : (size_t)new_off;
        cumulative += (int64_t)norm_ins - (int64_t)del_lens[i];
    }

    /* Apply operations in descending order so earlier offsets stay valid */
    for (i = 0; i < n; ++i) {
        size_t src = n - 1 - i;
        ops[i].offset = del_starts[src];
        ops[i].delete_len = del_lens[src];
        ops[i].insert_text = ins_texts[src];
        ops[i].insert_len = ins_lens[src];
    }

    ok = mc_edit_stack_apply_group(stack, kind, timestamp_ms,
                                   before, n, ops, n, NULL, 0);
    if (ok) {
        for (i = 0; i < n; ++i) {
            mc_pos_t p = { 0, 0 };
            (void)mc_pt_offset_to_pos(stack->tree, after_offsets[i], &p);
            after[i].anchor = p;
            after[i].active = p;
            after[i].preferred_x = 0.0f;
            after[i].has_preferred_x = false;
        }
        /* Update top edit group's after_sels */
        if (stack->index > 0) {
            mc_edit_group_t *g = &stack->groups[stack->index - 1];
            mc_selection_t *copy = (mc_selection_t *)malloc(n * sizeof(mc_selection_t));
            if (copy != NULL) {
                memcpy(copy, after, n * sizeof(mc_selection_t));
                free(g->after_sels);
                g->after_sels = copy;
                g->after_sel_count = n;
            }
        }
        (void)mc_cursors_set(cs, stack->tree, after, n);
    }

    free(ops);
    free(before);
    free(after);
    free(after_offsets);
    return ok;
}

int mc_cursors_insert_text(mc_cursor_set *cs, mc_edit_stack *stack,
                           const wchar_t *text, size_t len,
                           mc_edit_kind_t kind, uint64_t timestamp_ms)
{
    size_t n;
    size_t *starts;
    size_t *dlens;
    const wchar_t **texts;
    size_t *ilens;
    size_t i;
    int ok;

    if (cs == NULL || stack == NULL || stack->tree == NULL) return 0;
    mc_cursors_normalize(cs, stack->tree);
    n = cs->count;
    starts = (size_t *)calloc(n, sizeof(size_t));
    dlens = (size_t *)calloc(n, sizeof(size_t));
    texts = (const wchar_t **)calloc(n, sizeof(const wchar_t *));
    ilens = (size_t *)calloc(n, sizeof(size_t));
    if (!starts || !dlens || !texts || !ilens) {
        free(starts); free(dlens); free(texts); free(ilens);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        mc_range_t r = mc_selection_range(cs->items[i]);
        size_t s = 0, e = 0;
        (void)mc_pt_pos_to_offset(stack->tree, r.start, &s);
        (void)mc_pt_pos_to_offset(stack->tree, r.end, &e);
        starts[i] = s;
        dlens[i] = e - s;
        texts[i] = text;
        ilens[i] = len;
    }
    ok = mc_apply_per_cursor_edits(cs, stack, starts, dlens, texts, ilens,
                                   kind, timestamp_ms);
    free(starts); free(dlens); free(texts); free(ilens);
    return ok;
}

int mc_cursors_paste(mc_cursor_set *cs, mc_edit_stack *stack,
                     const wchar_t *text, size_t len, uint64_t timestamp_ms)
{
    size_t n;
    size_t line_count = 1;
    size_t i;
    if (cs == NULL || stack == NULL || stack->tree == NULL) return 0;
    mc_cursors_normalize(cs, stack->tree);
    n = cs->count;
    if (n <= 1 || len == 0) {
        return mc_cursors_insert_text(cs, stack, text, len, MC_EDIT_PASTE, timestamp_ms);
    }

    for (i = 0; i < len; ++i) {
        if (text[i] == L'\r') {
            ++line_count;
            if (i + 1 < len && text[i + 1] == L'\n') ++i;
        } else if (text[i] == L'\n') {
            ++line_count;
        }
    }
    if (line_count != n) {
        return mc_cursors_insert_text(cs, stack, text, len, MC_EDIT_PASTE, timestamp_ms);
    }

    {
        size_t *starts = (size_t *)calloc(n, sizeof(size_t));
        size_t *dlens = (size_t *)calloc(n, sizeof(size_t));
        const wchar_t **texts = (const wchar_t **)calloc(n, sizeof(const wchar_t *));
        size_t *ilens = (size_t *)calloc(n, sizeof(size_t));
        size_t pos = 0;
        int ok;
        if (!starts || !dlens || !texts || !ilens) {
            free(starts); free(dlens); free(texts); free(ilens);
            return 0;
        }
        for (i = 0; i < n; ++i) {
            mc_range_t r = mc_selection_range(cs->items[i]);
            size_t s = 0, e = 0;
            size_t line_start = pos;
            while (pos < len && text[pos] != L'\r' && text[pos] != L'\n') ++pos;
            (void)mc_pt_pos_to_offset(stack->tree, r.start, &s);
            (void)mc_pt_pos_to_offset(stack->tree, r.end, &e);
            starts[i] = s;
            dlens[i] = e - s;
            texts[i] = text + line_start;
            ilens[i] = pos - line_start;
            if (pos < len) {
                if (text[pos] == L'\r' && pos + 1 < len && text[pos + 1] == L'\n')
                    pos += 2;
                else
                    ++pos;
            }
        }
        ok = mc_apply_per_cursor_edits(cs, stack, starts, dlens, texts, ilens,
                                       MC_EDIT_PASTE, timestamp_ms);
        free(starts); free(dlens); free(texts); free(ilens);
        return ok;
    }
}

int mc_cursors_backspace(mc_cursor_set *cs, mc_edit_stack *stack,
                         bool by_word, uint64_t timestamp_ms)
{
    size_t n;
    size_t *starts;
    size_t *dlens;
    const wchar_t **texts;
    size_t *ilens;
    size_t i;
    int ok;

    if (cs == NULL || stack == NULL || stack->tree == NULL) return 0;
    mc_cursors_normalize(cs, stack->tree);
    n = cs->count;
    starts = (size_t *)calloc(n, sizeof(size_t));
    dlens = (size_t *)calloc(n, sizeof(size_t));
    texts = (const wchar_t **)calloc(n, sizeof(const wchar_t *));
    ilens = (size_t *)calloc(n, sizeof(size_t));
    if (!starts || !dlens || !texts || !ilens) {
        free(starts); free(dlens); free(texts); free(ilens);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        mc_range_t r = mc_selection_range(cs->items[i]);
        size_t s = 0, e = 0;
        if (mc_range_is_empty(r)) {
            mc_pos_t prev = by_word ? mc_pos_move_word_left(stack->tree, r.start)
                                    : mc_pos_move_left(stack->tree, r.start);
            (void)mc_pt_pos_to_offset(stack->tree, prev, &s);
            (void)mc_pt_pos_to_offset(stack->tree, r.end, &e);
        } else {
            (void)mc_pt_pos_to_offset(stack->tree, r.start, &s);
            (void)mc_pt_pos_to_offset(stack->tree, r.end, &e);
        }
        starts[i] = s;
        dlens[i] = e - s;
        texts[i] = L"";
        ilens[i] = 0;
    }
    ok = mc_apply_per_cursor_edits(cs, stack, starts, dlens, texts, ilens,
                                   MC_EDIT_DELETE, timestamp_ms);
    free(starts); free(dlens); free(texts); free(ilens);
    return ok;
}

int mc_cursors_delete_forward(mc_cursor_set *cs, mc_edit_stack *stack,
                              bool by_word, uint64_t timestamp_ms)
{
    size_t n;
    size_t *starts;
    size_t *dlens;
    const wchar_t **texts;
    size_t *ilens;
    size_t i;
    int ok;

    if (cs == NULL || stack == NULL || stack->tree == NULL) return 0;
    mc_cursors_normalize(cs, stack->tree);
    n = cs->count;
    starts = (size_t *)calloc(n, sizeof(size_t));
    dlens = (size_t *)calloc(n, sizeof(size_t));
    texts = (const wchar_t **)calloc(n, sizeof(const wchar_t *));
    ilens = (size_t *)calloc(n, sizeof(size_t));
    if (!starts || !dlens || !texts || !ilens) {
        free(starts); free(dlens); free(texts); free(ilens);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        mc_range_t r = mc_selection_range(cs->items[i]);
        size_t s = 0, e = 0;
        if (mc_range_is_empty(r)) {
            mc_pos_t next = by_word ? mc_pos_move_word_right(stack->tree, r.start)
                                    : mc_pos_move_right(stack->tree, r.start);
            (void)mc_pt_pos_to_offset(stack->tree, r.start, &s);
            (void)mc_pt_pos_to_offset(stack->tree, next, &e);
        } else {
            (void)mc_pt_pos_to_offset(stack->tree, r.start, &s);
            (void)mc_pt_pos_to_offset(stack->tree, r.end, &e);
        }
        starts[i] = s;
        dlens[i] = e - s;
        texts[i] = L"";
        ilens[i] = 0;
    }
    ok = mc_apply_per_cursor_edits(cs, stack, starts, dlens, texts, ilens,
                                   MC_EDIT_DELETE, timestamp_ms);
    free(starts); free(dlens); free(texts); free(ilens);
    return ok;
}

int mc_cursors_indent(mc_cursor_set *cs, mc_edit_stack *stack,
                      uint32_t tab_size, bool insert_spaces,
                      bool outdent, uint64_t timestamp_ms)
{
    wchar_t spaces[32];
    const wchar_t *indent_str;
    size_t indent_len;
    size_t i;
    bool any_multi_line = false;

    if (cs == NULL || stack == NULL || stack->tree == NULL) return 0;
    if (tab_size == 0) tab_size = 4;
    if (tab_size > 16) tab_size = 16;

    if (insert_spaces) {
        for (i = 0; i < tab_size; ++i) spaces[i] = L' ';
        indent_str = spaces;
        indent_len = tab_size;
    } else {
        spaces[0] = L'\t';
        indent_str = spaces;
        indent_len = 1;
    }

    mc_cursors_normalize(cs, stack->tree);
    for (i = 0; i < cs->count; ++i) {
        mc_range_t r = mc_selection_range(cs->items[i]);
        if (r.start.line != r.end.line) any_multi_line = true;
    }

    if (!outdent && !any_multi_line) {
        return mc_cursors_insert_text(cs, stack, indent_str, indent_len,
                                      MC_EDIT_OTHER, timestamp_ms);
    }

    /* Line-level indent/outdent across all touched lines */
    {
        size_t lc = mc_pt_line_count(stack->tree);
        uint32_t *lines = NULL;
        size_t line_n = 0, line_cap = 0;
        mc_edit_op_t *ops = NULL;
        size_t op_n = 0;
        mc_wbuf lbuf;
        int ok = 1;

        mc_wbuf_init(&lbuf);
        for (i = 0; i < cs->count; ++i) {
            mc_range_t r = mc_selection_range(cs->items[i]);
            uint32_t l_end = r.end.line;
            uint32_t l;
            if (l_end > r.start.line && r.end.col == 0) --l_end;
            for (l = r.start.line; l <= l_end && (size_t)l < lc; ++l) {
                if (line_n == 0 || lines[line_n - 1] < l) {
                    if (!mc_vec_reserve((void **)&lines, &line_cap,
                                        sizeof(uint32_t), line_n + 1)) {
                        ok = 0;
                        break;
                    }
                    lines[line_n++] = l;
                }
            }
        }
        if (ok && line_n > 0) {
            ops = (mc_edit_op_t *)calloc(line_n, sizeof(mc_edit_op_t));
            if (ops == NULL) {
                ok = 0;
            } else {
                for (i = 0; i < line_n; ++i) {
                    uint32_t l = lines[line_n - 1 - i];
                    size_t off = mc_pt_line_start_offset(stack->tree, l);
                    if (!outdent) {
                        ops[op_n].offset = off;
                        ops[op_n].delete_len = 0;
                        ops[op_n].insert_text = indent_str;
                        ops[op_n].insert_len = indent_len;
                        ++op_n;
                    } else if (mc_pt_get_line(stack->tree, l, &lbuf) && lbuf.length > 0) {
                        size_t del = 0;
                        if (lbuf.data[0] == L'\t') {
                            del = 1;
                        } else {
                            while (del < lbuf.length && del < tab_size &&
                                   lbuf.data[del] == L' ') {
                                ++del;
                            }
                        }
                        if (del > 0) {
                            ops[op_n].offset = off;
                            ops[op_n].delete_len = del;
                            ops[op_n].insert_text = NULL;
                            ops[op_n].insert_len = 0;
                            ++op_n;
                        }
                    }
                }
                if (op_n > 0) {
                    ok = mc_edit_stack_apply_group(stack, MC_EDIT_FORMAT, timestamp_ms,
                                                   cs->items, cs->count,
                                                   ops, op_n, cs->items, cs->count);
                    mc_cursors_normalize(cs, stack->tree);
                }
                free(ops);
            }
        }
        free(lines);
        mc_wbuf_destroy(&lbuf);
        return ok;
    }
}

int mc_cursors_copy_text(const mc_cursor_set *cs, const mc_piece_tree *tree,
                         mc_wbuf *out_wbuf)
{
    size_t i;
    mc_wbuf seg;
    bool crlf;
    if (cs == NULL || tree == NULL || out_wbuf == NULL) return 0;
    mc_wbuf_clear(out_wbuf);
    mc_wbuf_init(&seg);
    crlf = (mc_pt_get_eol_mode(tree) == MC_EOL_CRLF);

    for (i = 0; i < cs->count; ++i) {
        mc_range_t r = mc_selection_range(cs->items[i]);
        size_t s = 0, e = 0;
        size_t k;
        if (i > 0) {
            if (crlf) {
                if (!mc_wbuf_append(out_wbuf, L"\r\n", 2)) goto fail;
            } else {
                if (!mc_wbuf_append_char(out_wbuf, L'\n')) goto fail;
            }
        }
        if (mc_range_is_empty(r)) {
            /* Copy whole line when single cursor has empty selection (VS Code style) */
            if (cs->count == 1) {
                if (!mc_pt_get_line(tree, r.start.line, &seg)) goto fail;
                if (!mc_wbuf_append(out_wbuf, seg.data, seg.length)) goto fail;
                if (crlf) {
                    if (!mc_wbuf_append(out_wbuf, L"\r\n", 2)) goto fail;
                } else {
                    if (!mc_wbuf_append_char(out_wbuf, L'\n')) goto fail;
                }
            }
            continue;
        }
        (void)mc_pt_pos_to_offset(tree, r.start, &s);
        (void)mc_pt_pos_to_offset(tree, r.end, &e);
        if (!mc_pt_get_range(tree, s, e - s, &seg)) goto fail;
        for (k = 0; k < seg.length; ++k) {
            if (seg.data[k] == L'\n' && crlf) {
                if (!mc_wbuf_append(out_wbuf, L"\r\n", 2)) goto fail;
            } else {
                if (!mc_wbuf_append_char(out_wbuf, seg.data[k])) goto fail;
            }
        }
    }
    mc_wbuf_destroy(&seg);
    return 1;
fail:
    mc_wbuf_destroy(&seg);
    return 0;
}
