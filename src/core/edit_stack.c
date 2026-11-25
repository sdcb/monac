#include "edit_stack.h"

#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>

static void mc_atomic_edit_free(mc_atomic_edit_t *edit)
{
    free(edit->deleted_text);
    free(edit->inserted_text);
    edit->deleted_text = NULL;
    edit->inserted_text = NULL;
    edit->deleted_len = 0;
    edit->inserted_len = 0;
}

static void mc_edit_group_free(mc_edit_group_t *g)
{
    size_t i;
    free(g->before_sels);
    free(g->after_sels);
    for (i = 0; i < g->edit_count; ++i) {
        mc_atomic_edit_free(&g->edits[i]);
    }
    free(g->edits);
    memset(g, 0, sizeof(*g));
}

static mc_selection_t *mc_dup_sels(const mc_selection_t *sels, size_t count)
{
    mc_selection_t *copy;
    if (sels == NULL || count == 0) return NULL;
    copy = (mc_selection_t *)malloc(count * sizeof(mc_selection_t));
    if (copy == NULL) return NULL;
    memcpy(copy, sels, count * sizeof(mc_selection_t));
    return copy;
}

static int mc_is_merge_breaker_char(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r';
}

static void mc_notify_delta(const mc_edit_stack *stack,
                            size_t offset, size_t deleted_len, size_t inserted_len)
{
    if (stack->observer != NULL && (deleted_len != 0 || inserted_len != 0)) {
        mc_edit_delta_t d;
        d.offset = offset;
        d.deleted_len = deleted_len;
        d.inserted_len = inserted_len;
        stack->observer(stack->observer_ud, &d);
    }
}

void mc_edit_stack_init(mc_edit_stack *stack, mc_piece_tree *tree)
{
    if (stack == NULL) return;
    memset(stack, 0, sizeof(*stack));
    stack->tree = tree;
    stack->merge_window_ms = 1000;
}

void mc_edit_stack_destroy(mc_edit_stack *stack)
{
    if (stack == NULL) return;
    mc_edit_stack_clear(stack);
    free(stack->groups);
    memset(stack, 0, sizeof(*stack));
}

void mc_edit_stack_clear(mc_edit_stack *stack)
{
    size_t i;
    if (stack == NULL) return;
    for (i = 0; i < stack->count; ++i) {
        mc_edit_group_free(&stack->groups[i]);
    }
    stack->count = 0;
    stack->index = 0;
}

void mc_edit_stack_set_observer(mc_edit_stack *stack,
                                mc_edit_observer_fn observer, void *userdata)
{
    if (stack == NULL) return;
    stack->observer = observer;
    stack->observer_ud = userdata;
}

void mc_edit_stack_push_undo_stop(mc_edit_stack *stack)
{
    if (stack == NULL || stack->index == 0) return;
    stack->groups[stack->index - 1].closed = true;
}

static int mc_try_merge_insert(mc_edit_stack *stack,
                               mc_edit_kind_t kind,
                               uint64_t timestamp_ms,
                               const mc_edit_op_t *op,
                               const wchar_t *norm_text, size_t norm_len,
                               const mc_selection_t *after_sels, size_t after_count)
{
    mc_edit_group_t *prev;
    mc_atomic_edit_t *ae;
    wchar_t *grown;
    mc_selection_t *new_after;

    if (kind != MC_EDIT_INSERT_CHAR || stack->index == 0 ||
        stack->index != stack->count) return 0;
    if (op->delete_len != 0 || norm_len == 0 || norm_len > 2) return 0;
    if (mc_is_merge_breaker_char(norm_text[0])) return 0;

    prev = &stack->groups[stack->index - 1];
    if (prev->closed || prev->kind != MC_EDIT_INSERT_CHAR || prev->edit_count != 1)
        return 0;
    if (timestamp_ms < prev->timestamp_ms ||
        timestamp_ms - prev->timestamp_ms > stack->merge_window_ms)
        return 0;

    ae = &prev->edits[0];
    if (ae->deleted_len != 0 || ae->inserted_len == 0) return 0;
    if (mc_is_merge_breaker_char(ae->inserted_text[ae->inserted_len - 1])) return 0;
    if (op->offset != ae->offset + ae->inserted_len) return 0;

    grown = (wchar_t *)realloc(ae->inserted_text,
                               (ae->inserted_len + norm_len) * sizeof(wchar_t));
    if (grown == NULL) return 0;
    memcpy(grown + ae->inserted_len, norm_text, norm_len * sizeof(wchar_t));
    ae->inserted_text = grown;
    ae->inserted_len += norm_len;

    new_after = mc_dup_sels(after_sels, after_count);
    if (after_count != 0 && new_after == NULL) return 0;
    free(prev->after_sels);
    prev->after_sels = new_after;
    prev->after_sel_count = after_count;
    prev->timestamp_ms = timestamp_ms;
    return 1;
}

static int mc_try_merge_delete(mc_edit_stack *stack,
                               mc_edit_kind_t kind,
                               uint64_t timestamp_ms,
                               const mc_edit_op_t *op,
                               const wchar_t *del_text, size_t del_len,
                               const mc_selection_t *after_sels, size_t after_count)
{
    mc_edit_group_t *prev;
    mc_atomic_edit_t *ae;
    wchar_t *grown;
    mc_selection_t *new_after;

    if (kind != MC_EDIT_DELETE || stack->index == 0 ||
        stack->index != stack->count) return 0;
    if (op->insert_len != 0 || del_len == 0 || del_len > 2) return 0;
    if (mc_is_merge_breaker_char(del_text[0])) return 0;

    prev = &stack->groups[stack->index - 1];
    if (prev->closed || prev->kind != MC_EDIT_DELETE || prev->edit_count != 1)
        return 0;
    if (timestamp_ms < prev->timestamp_ms ||
        timestamp_ms - prev->timestamp_ms > stack->merge_window_ms)
        return 0;

    ae = &prev->edits[0];
    if (ae->inserted_len != 0 || ae->deleted_len == 0) return 0;
    if (mc_is_merge_breaker_char(ae->deleted_text[0])) return 0;
    if (op->offset + del_len != ae->offset) return 0;

    grown = (wchar_t *)malloc((ae->deleted_len + del_len) * sizeof(wchar_t));
    if (grown == NULL) return 0;
    memcpy(grown, del_text, del_len * sizeof(wchar_t));
    memcpy(grown + del_len, ae->deleted_text, ae->deleted_len * sizeof(wchar_t));
    free(ae->deleted_text);
    ae->deleted_text = grown;
    ae->deleted_len += del_len;
    ae->offset = op->offset;

    new_after = mc_dup_sels(after_sels, after_count);
    if (after_count != 0 && new_after == NULL) return 0;
    free(prev->after_sels);
    prev->after_sels = new_after;
    prev->after_sel_count = after_count;
    prev->timestamp_ms = timestamp_ms;
    return 1;
}

int mc_edit_stack_apply_group(mc_edit_stack *stack,
                              mc_edit_kind_t kind,
                              uint64_t timestamp_ms,
                              const mc_selection_t *before_sels, size_t before_count,
                              const mc_edit_op_t *ops, size_t op_count,
                              const mc_selection_t *after_sels, size_t after_count)
{
    mc_atomic_edit_t *recorded = NULL;
    size_t i;
    mc_wbuf tmp;
    int merged = 0;

    if (stack == NULL || stack->tree == NULL || (ops == NULL && op_count != 0))
        return 0;
    if (op_count == 0) return 1;

    recorded = (mc_atomic_edit_t *)calloc(op_count, sizeof(mc_atomic_edit_t));
    if (recorded == NULL) return 0;

    mc_wbuf_init(&tmp);
    for (i = 0; i < op_count; ++i) {
        const mc_edit_op_t *op = &ops[i];
        size_t norm_ins = 0;
        if (op->offset > mc_pt_length(stack->tree) ||
            op->delete_len > mc_pt_length(stack->tree) - op->offset) {
            goto rollback;
        }
        recorded[i].offset = op->offset;
        if (op->delete_len > 0) {
            if (!mc_pt_get_range(stack->tree, op->offset, op->delete_len, &tmp))
                goto rollback;
            recorded[i].deleted_text =
                (wchar_t *)malloc(op->delete_len * sizeof(wchar_t));
            if (recorded[i].deleted_text == NULL) goto rollback;
            memcpy(recorded[i].deleted_text, tmp.data,
                   op->delete_len * sizeof(wchar_t));
            recorded[i].deleted_len = op->delete_len;
            if (!mc_pt_delete(stack->tree, op->offset, op->delete_len))
                goto rollback;
        }
        if (op->insert_len > 0) {
            if (!mc_pt_insert_ex(stack->tree, op->offset,
                                 op->insert_text, op->insert_len, &norm_ins)) {
                if (recorded[i].deleted_len > 0) {
                    (void)mc_pt_insert(stack->tree, op->offset,
                                       recorded[i].deleted_text,
                                       recorded[i].deleted_len);
                }
                goto rollback;
            }
            if (norm_ins > 0) {
                if (!mc_pt_get_range(stack->tree, op->offset, norm_ins, &tmp)) {
                    (void)mc_pt_delete(stack->tree, op->offset, norm_ins);
                    if (recorded[i].deleted_len > 0) {
                        (void)mc_pt_insert(stack->tree, op->offset,
                                           recorded[i].deleted_text,
                                           recorded[i].deleted_len);
                    }
                    goto rollback;
                }
                recorded[i].inserted_text =
                    (wchar_t *)malloc(norm_ins * sizeof(wchar_t));
                if (recorded[i].inserted_text == NULL) {
                    (void)mc_pt_delete(stack->tree, op->offset, norm_ins);
                    if (recorded[i].deleted_len > 0) {
                        (void)mc_pt_insert(stack->tree, op->offset,
                                           recorded[i].deleted_text,
                                           recorded[i].deleted_len);
                    }
                    goto rollback;
                }
                memcpy(recorded[i].inserted_text, tmp.data,
                       norm_ins * sizeof(wchar_t));
                recorded[i].inserted_len = norm_ins;
            }
        }
        mc_notify_delta(stack, op->offset,
                        recorded[i].deleted_len, recorded[i].inserted_len);
    }
    mc_wbuf_destroy(&tmp);

    if (op_count == 1 && before_count <= 1 && after_count <= 1) {
        if (kind == MC_EDIT_INSERT_CHAR && recorded[0].deleted_len == 0 &&
            recorded[0].inserted_len > 0) {
            merged = mc_try_merge_insert(stack, kind, timestamp_ms, &ops[0],
                                         recorded[0].inserted_text,
                                         recorded[0].inserted_len,
                                         after_sels, after_count);
        } else if (kind == MC_EDIT_DELETE && recorded[0].inserted_len == 0 &&
                   recorded[0].deleted_len > 0) {
            merged = mc_try_merge_delete(stack, kind, timestamp_ms, &ops[0],
                                         recorded[0].deleted_text,
                                         recorded[0].deleted_len,
                                         after_sels, after_count);
        }
    }

    if (merged) {
        mc_atomic_edit_free(&recorded[0]);
        free(recorded);
        ++stack->version_id;
        return 1;
    }

    while (stack->count > stack->index) {
        mc_edit_group_free(&stack->groups[stack->count - 1]);
        --stack->count;
    }
    if (!mc_vec_reserve((void **)&stack->groups, &stack->capacity,
                        sizeof(mc_edit_group_t), stack->count + 1)) {
        for (i = 0; i < op_count; ++i) mc_atomic_edit_free(&recorded[i]);
        free(recorded);
        return 0;
    }
    {
        mc_edit_group_t *g = &stack->groups[stack->count];
        memset(g, 0, sizeof(*g));
        g->before_sels = mc_dup_sels(before_sels, before_count);
        g->before_sel_count = before_count;
        g->after_sels = mc_dup_sels(after_sels, after_count);
        g->after_sel_count = after_count;
        g->edits = recorded;
        g->edit_count = op_count;
        g->timestamp_ms = timestamp_ms;
        g->kind = kind;
        g->closed = (kind != MC_EDIT_INSERT_CHAR && kind != MC_EDIT_DELETE);
        if (op_count == 1 && kind == MC_EDIT_INSERT_CHAR &&
            recorded[0].inserted_len > 0 &&
            mc_is_merge_breaker_char(
                recorded[0].inserted_text[recorded[0].inserted_len - 1])) {
            g->closed = true;
        }
        ++stack->count;
        stack->index = stack->count;
        ++stack->version_id;
    }
    return 1;

rollback:
    mc_wbuf_destroy(&tmp);
    while (i > 0) {
        --i;
        if (recorded[i].inserted_len > 0) {
            (void)mc_pt_delete(stack->tree, recorded[i].offset,
                               recorded[i].inserted_len);
        }
        if (recorded[i].deleted_len > 0) {
            (void)mc_pt_insert(stack->tree, recorded[i].offset,
                               recorded[i].deleted_text,
                               recorded[i].deleted_len);
        }
        mc_notify_delta(stack, recorded[i].offset,
                        recorded[i].inserted_len, recorded[i].deleted_len);
        mc_atomic_edit_free(&recorded[i]);
    }
    free(recorded);
    return 0;
}

int mc_edit_stack_apply(mc_edit_stack *stack, size_t offset, size_t delete_length,
                        const wchar_t *insert_text, size_t insert_length)
{
    mc_edit_op_t op;
    op.offset = offset;
    op.delete_len = delete_length;
    op.insert_text = insert_text;
    op.insert_len = insert_length;
    return mc_edit_stack_apply_group(stack, MC_EDIT_OTHER, 0,
                                     NULL, 0, &op, 1, NULL, 0);
}

int mc_edit_stack_undo(mc_edit_stack *stack,
                       mc_selection_t **out_sels, size_t *out_sel_count)
{
    mc_edit_group_t *g;
    size_t i;
    if (out_sels != NULL) *out_sels = NULL;
    if (out_sel_count != NULL) *out_sel_count = 0;
    if (stack == NULL || stack->tree == NULL || stack->index == 0) return 0;

    g = &stack->groups[stack->index - 1];
    for (i = g->edit_count; i > 0; --i) {
        const mc_atomic_edit_t *ae = &g->edits[i - 1];
        if (ae->inserted_len > 0) {
            if (!mc_pt_delete(stack->tree, ae->offset, ae->inserted_len))
                return 0;
        }
        if (ae->deleted_len > 0) {
            if (!mc_pt_insert(stack->tree, ae->offset,
                              ae->deleted_text, ae->deleted_len))
                return 0;
        }
        mc_notify_delta(stack, ae->offset, ae->inserted_len, ae->deleted_len);
    }
    --stack->index;
    ++stack->version_id;
    if (out_sels != NULL) *out_sels = g->before_sels;
    if (out_sel_count != NULL) *out_sel_count = g->before_sel_count;
    return 1;
}

int mc_edit_stack_redo(mc_edit_stack *stack,
                       mc_selection_t **out_sels, size_t *out_sel_count)
{
    mc_edit_group_t *g;
    size_t i;
    if (out_sels != NULL) *out_sels = NULL;
    if (out_sel_count != NULL) *out_sel_count = 0;
    if (stack == NULL || stack->tree == NULL || stack->index >= stack->count)
        return 0;

    g = &stack->groups[stack->index];
    for (i = 0; i < g->edit_count; ++i) {
        const mc_atomic_edit_t *ae = &g->edits[i];
        if (ae->deleted_len > 0) {
            if (!mc_pt_delete(stack->tree, ae->offset, ae->deleted_len))
                return 0;
        }
        if (ae->inserted_len > 0) {
            if (!mc_pt_insert(stack->tree, ae->offset,
                              ae->inserted_text, ae->inserted_len))
                return 0;
        }
        mc_notify_delta(stack, ae->offset, ae->deleted_len, ae->inserted_len);
    }
    ++stack->index;
    ++stack->version_id;
    if (out_sels != NULL) *out_sels = g->after_sels;
    if (out_sel_count != NULL) *out_sel_count = g->after_sel_count;
    return 1;
}

bool mc_edit_stack_can_undo(const mc_edit_stack *stack)
{
    return stack != NULL && stack->index > 0;
}

bool mc_edit_stack_can_redo(const mc_edit_stack *stack)
{
    return stack != NULL && stack->index < stack->count;
}

uint64_t mc_edit_stack_version(const mc_edit_stack *stack)
{
    return stack == NULL ? 0 : stack->version_id;
}

void mc_edit_stack_bump_version(mc_edit_stack *stack)
{
    if (stack != NULL) ++stack->version_id;
}
