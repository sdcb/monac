#include "search.h"

#include "../base/strbuf.h"
#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

void mc_search_init(mc_search_state *st)
{
    if (st == NULL) return;
    memset(st, 0, sizeof(*st));
}

void mc_search_destroy(mc_search_state *st)
{
    if (st == NULL) return;
    free(st->query);
    free(st->matches);
    memset(st, 0, sizeof(*st));
}

static int mc_wchars_equal(wchar_t a, wchar_t b, bool case_sensitive)
{
    if (case_sensitive) return a == b;
    return towlower((wint_t)a) == towlower((wint_t)b);
}

int mc_search_update(mc_search_state *st, const mc_piece_tree *tree,
                     const wchar_t *query, size_t query_len,
                     mc_search_options_t options)
{
    mc_wbuf text;
    size_t i;
    wchar_t *qcopy = NULL;

    if (st == NULL || tree == NULL || (query == NULL && query_len != 0)) return 0;

    if (query_len > 0) {
        qcopy = (wchar_t *)malloc((query_len + 1) * sizeof(wchar_t));
        if (qcopy == NULL) return 0;
        memcpy(qcopy, query, query_len * sizeof(wchar_t));
        qcopy[query_len] = L'\0';
    }
    free(st->query);
    st->query = qcopy;
    st->query_len = query_len;
    st->options = options;
    st->match_count = 0;
    st->current_index = 0;
    st->has_current = false;

    if (query_len == 0) return 1;

    mc_wbuf_init(&text);
    if (!mc_pt_get_range(tree, 0, mc_pt_length(tree), &text)) {
        mc_wbuf_destroy(&text);
        return 0;
    }

    if (text.length >= query_len) {
        i = 0;
        while (i + query_len <= text.length) {
            size_t j;
            bool match = true;
            for (j = 0; j < query_len; ++j) {
                if (!mc_wchars_equal(text.data[i + j], query[j], options.case_sensitive)) {
                    match = false;
                    break;
                }
            }
            if (match && options.whole_word) {
                bool left_ok = (i == 0) || !mc_is_word_char(text.data[i - 1]);
                bool right_ok = (i + query_len == text.length) ||
                                !mc_is_word_char(text.data[i + query_len]);
                if (!left_ok || !right_ok) match = false;
            }
            if (match) {
                mc_search_match_t m;
                if (!mc_vec_reserve((void **)&st->matches, &st->match_cap,
                                    sizeof(mc_search_match_t), st->match_count + 1)) {
                    mc_wbuf_destroy(&text);
                    return 0;
                }
                m.start_offset = i;
                m.end_offset = i + query_len;
                (void)mc_pt_offset_to_pos(tree, m.start_offset, &m.range.start);
                (void)mc_pt_offset_to_pos(tree, m.end_offset, &m.range.end);
                st->matches[st->match_count++] = m;
                i += query_len;
            } else {
                ++i;
            }
        }
    }

    mc_wbuf_destroy(&text);
    if (st->match_count > 0) {
        st->has_current = true;
        st->current_index = 0;
    }
    return 1;
}

int mc_search_find_next(mc_search_state *st, size_t from_offset, bool wrap,
                        mc_search_match_t *out_match)
{
    size_t i;
    if (st == NULL || st->match_count == 0) return 0;
    for (i = 0; i < st->match_count; ++i) {
        if (st->matches[i].start_offset >= from_offset) {
            st->current_index = i;
            st->has_current = true;
            if (out_match != NULL) *out_match = st->matches[i];
            return 1;
        }
    }
    if (wrap) {
        st->current_index = 0;
        st->has_current = true;
        if (out_match != NULL) *out_match = st->matches[0];
        return 1;
    }
    return 0;
}

int mc_search_find_prev(mc_search_state *st, size_t from_offset, bool wrap,
                        mc_search_match_t *out_match)
{
    size_t i;
    if (st == NULL || st->match_count == 0) return 0;
    for (i = st->match_count; i > 0; --i) {
        if (st->matches[i - 1].start_offset < from_offset) {
            st->current_index = i - 1;
            st->has_current = true;
            if (out_match != NULL) *out_match = st->matches[i - 1];
            return 1;
        }
    }
    if (wrap) {
        st->current_index = st->match_count - 1;
        st->has_current = true;
        if (out_match != NULL) *out_match = st->matches[st->current_index];
        return 1;
    }
    return 0;
}

void mc_search_apply_decorations(const mc_search_state *st, mc_dec_tree *decs,
                                 uint32_t owner_id)
{
    mc_dec_spec_t *specs;
    size_t i;
    if (st == NULL || decs == NULL) return;
    if (st->match_count == 0) {
        mc_dec_remove_by_owner(decs, owner_id);
        return;
    }
    specs = (mc_dec_spec_t *)calloc(st->match_count, sizeof(mc_dec_spec_t));
    if (specs == NULL) return;
    for (i = 0; i < st->match_count; ++i) {
        bool is_cur = st->has_current && i == st->current_index;
        specs[i].start = st->matches[i].start_offset;
        specs[i].end = st->matches[i].end_offset;
        specs[i].stickiness = MC_STICKY_NEVER_GROWS;
        specs[i].collapse_on_delete = true;
        specs[i].style.bg_color = is_cur ? 0x99EA8C2Au : 0x55EAB308u;
        specs[i].style.border_color = is_cur ? 0xFFFFB74Du : 0u;
    }
    (void)mc_dec_replace_owner(decs, owner_id, specs, st->match_count);
    free(specs);
}

int mc_search_replace_current(mc_search_state *st, mc_edit_stack *stack,
                              mc_cursor_set *cursors,
                              const wchar_t *replacement, size_t repl_len,
                              uint64_t timestamp_ms)
{
    mc_search_match_t m;
    mc_edit_op_t op;
    mc_selection_t before_sel = { { 0, 0 }, { 0, 0 }, 0.0f, false };
    mc_selection_t after_sel = { { 0, 0 }, { 0, 0 }, 0.0f, false };
    size_t next_from;
    int ok;

    if (st == NULL || stack == NULL || stack->tree == NULL ||
        !st->has_current || st->current_index >= st->match_count) return 0;

    m = st->matches[st->current_index];
    op.offset = m.start_offset;
    op.delete_len = m.end_offset - m.start_offset;
    op.insert_text = replacement;
    op.insert_len = repl_len;

    if (cursors != NULL) before_sel = mc_cursors_primary(cursors);
    ok = mc_edit_stack_apply_group(stack, MC_EDIT_OTHER, timestamp_ms,
                                   cursors ? cursors->items : &before_sel,
                                   cursors ? cursors->count : 1,
                                   &op, 1, NULL, 0);
    if (!ok) return 0;

    next_from = m.start_offset + repl_len;
    (void)mc_pt_offset_to_pos(stack->tree, next_from, &after_sel.anchor);
    after_sel.active = after_sel.anchor;
    if (cursors != NULL) {
        mc_cursors_set_single(cursors, after_sel.anchor, after_sel.active);
    }
    if (stack->index > 0) {
        mc_edit_group_t *g = &stack->groups[stack->index - 1];
        mc_selection_t *copy = (mc_selection_t *)malloc(sizeof(mc_selection_t));
        if (copy != NULL) {
            *copy = after_sel;
            free(g->after_sels);
            g->after_sels = copy;
            g->after_sel_count = 1;
        }
    }
    if (st->query != NULL) {
        wchar_t *q = (wchar_t *)malloc((st->query_len + 1) * sizeof(wchar_t));
        if (q != NULL) {
            size_t qlen = st->query_len;
            memcpy(q, st->query, (qlen + 1) * sizeof(wchar_t));
            (void)mc_search_update(st, stack->tree, q, qlen, st->options);
            (void)mc_search_find_next(st, next_from, true, NULL);
            free(q);
        }
    }
    return 1;
}

size_t mc_search_replace_all(mc_search_state *st, mc_edit_stack *stack,
                             mc_cursor_set *cursors,
                             const wchar_t *replacement, size_t repl_len,
                             uint64_t timestamp_ms)
{
    size_t count;
    mc_edit_op_t *ops;
    size_t i;
    int ok;

    if (st == NULL || stack == NULL || stack->tree == NULL || st->match_count == 0)
        return 0;
    count = st->match_count;
    ops = (mc_edit_op_t *)calloc(count, sizeof(mc_edit_op_t));
    if (ops == NULL) return 0;

    /* Apply in descending offset order inside ONE undo group */
    for (i = 0; i < count; ++i) {
        const mc_search_match_t *m = &st->matches[count - 1 - i];
        ops[i].offset = m->start_offset;
        ops[i].delete_len = m->end_offset - m->start_offset;
        ops[i].insert_text = replacement;
        ops[i].insert_len = repl_len;
    }

    ok = mc_edit_stack_apply_group(stack, MC_EDIT_REPLACE_ALL, timestamp_ms,
                                   cursors ? cursors->items : NULL,
                                   cursors ? cursors->count : 0,
                                   ops, count,
                                   cursors ? cursors->items : NULL,
                                   cursors ? cursors->count : 0);
    free(ops);
    if (!ok) return 0;

    if (cursors != NULL) mc_cursors_normalize(cursors, stack->tree);
    if (st->query != NULL) {
        wchar_t *q = (wchar_t *)malloc((st->query_len + 1) * sizeof(wchar_t));
        if (q != NULL) {
            size_t qlen = st->query_len;
            memcpy(q, st->query, (qlen + 1) * sizeof(wchar_t));
            (void)mc_search_update(st, stack->tree, q, qlen, st->options);
            free(q);
        }
    }
    return count;
}
