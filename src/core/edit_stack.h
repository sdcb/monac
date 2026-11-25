#ifndef MONAC_CORE_EDIT_STACK_H
#define MONAC_CORE_EDIT_STACK_H

#include "piece_tree.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mc_edit_kind_t {
    MC_EDIT_OTHER = 0,
    MC_EDIT_INSERT_CHAR,
    MC_EDIT_DELETE,
    MC_EDIT_PASTE,
    MC_EDIT_FORMAT,
    MC_EDIT_REPLACE_ALL
} mc_edit_kind_t;

typedef struct mc_edit_op_t {
    size_t offset;
    size_t delete_len;
    const wchar_t *insert_text;
    size_t insert_len;
} mc_edit_op_t;

typedef struct mc_atomic_edit_t {
    size_t offset;
    wchar_t *deleted_text;
    size_t deleted_len;
    wchar_t *inserted_text;
    size_t inserted_len;
} mc_atomic_edit_t;

typedef struct mc_edit_delta_t {
    size_t offset;
    size_t deleted_len;
    size_t inserted_len;
} mc_edit_delta_t;

typedef void (*mc_edit_observer_fn)(void *userdata, const mc_edit_delta_t *delta);

typedef struct mc_edit_group_t {
    mc_selection_t *before_sels;
    size_t before_sel_count;
    mc_selection_t *after_sels;
    size_t after_sel_count;
    mc_atomic_edit_t *edits;
    size_t edit_count;
    uint64_t timestamp_ms;
    mc_edit_kind_t kind;
    bool closed;
} mc_edit_group_t;

typedef struct mc_edit_stack {
    mc_piece_tree *tree;
    mc_edit_group_t *groups;
    size_t count;
    size_t capacity;
    size_t index;
    uint64_t version_id;
    uint64_t merge_window_ms;
    mc_edit_observer_fn observer;
    void *observer_ud;
} mc_edit_stack;

void mc_edit_stack_init(mc_edit_stack *stack, mc_piece_tree *tree);
void mc_edit_stack_destroy(mc_edit_stack *stack);
void mc_edit_stack_clear(mc_edit_stack *stack);
void mc_edit_stack_set_observer(mc_edit_stack *stack,
                                mc_edit_observer_fn observer, void *userdata);
void mc_edit_stack_push_undo_stop(mc_edit_stack *stack);

int mc_edit_stack_apply_group(mc_edit_stack *stack,
                              mc_edit_kind_t kind,
                              uint64_t timestamp_ms,
                              const mc_selection_t *before_sels, size_t before_count,
                              const mc_edit_op_t *ops, size_t op_count,
                              const mc_selection_t *after_sels, size_t after_count);

int mc_edit_stack_apply(mc_edit_stack *stack, size_t offset, size_t delete_length,
                        const wchar_t *insert_text, size_t insert_length);

int mc_edit_stack_undo(mc_edit_stack *stack,
                       mc_selection_t **out_sels, size_t *out_sel_count);
int mc_edit_stack_redo(mc_edit_stack *stack,
                       mc_selection_t **out_sels, size_t *out_sel_count);

bool mc_edit_stack_can_undo(const mc_edit_stack *stack);
bool mc_edit_stack_can_redo(const mc_edit_stack *stack);
uint64_t mc_edit_stack_version(const mc_edit_stack *stack);
void mc_edit_stack_bump_version(mc_edit_stack *stack);

#endif /* MONAC_CORE_EDIT_STACK_H */
