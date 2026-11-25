#ifndef MONAC_CORE_CURSOR_H
#define MONAC_CORE_CURSOR_H

#include "../base/strbuf.h"
#include "edit_stack.h"
#include "piece_tree.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct mc_cursor_set {
    mc_selection_t *items;
    size_t count;
    size_t capacity;
    size_t primary_index;
} mc_cursor_set;

void mc_cursors_init(mc_cursor_set *cs);
void mc_cursors_destroy(mc_cursor_set *cs);

void mc_cursors_set_single(mc_cursor_set *cs, mc_pos_t anchor, mc_pos_t active);
int mc_cursors_set(mc_cursor_set *cs, const mc_piece_tree *tree,
                   const mc_selection_t *sels, size_t count);
int mc_cursors_add(mc_cursor_set *cs, const mc_piece_tree *tree, mc_selection_t sel);
void mc_cursors_collapse_to_primary(mc_cursor_set *cs);
void mc_cursors_select_all(mc_cursor_set *cs, const mc_piece_tree *tree);

mc_selection_t mc_cursors_primary(const mc_cursor_set *cs);
void mc_cursors_normalize(mc_cursor_set *cs, const mc_piece_tree *tree);

/* Word & character boundary helpers (surrogate-safe, UTF-16) */
bool mc_is_word_char(wchar_t ch);
mc_pos_t mc_pos_clamp(const mc_piece_tree *tree, mc_pos_t pos);
mc_pos_t mc_pos_move_left(const mc_piece_tree *tree, mc_pos_t pos);
mc_pos_t mc_pos_move_right(const mc_piece_tree *tree, mc_pos_t pos);
mc_pos_t mc_pos_move_word_left(const mc_piece_tree *tree, mc_pos_t pos);
mc_pos_t mc_pos_move_word_right(const mc_piece_tree *tree, mc_pos_t pos);
mc_range_t mc_word_range_at(const mc_piece_tree *tree, mc_pos_t pos);

/* Multi-cursor navigation */
void mc_cursors_move_left(mc_cursor_set *cs, const mc_piece_tree *tree,
                          bool extend, bool by_word);
void mc_cursors_move_right(mc_cursor_set *cs, const mc_piece_tree *tree,
                           bool extend, bool by_word);
void mc_cursors_move_up(mc_cursor_set *cs, const mc_piece_tree *tree, bool extend);
void mc_cursors_move_down(mc_cursor_set *cs, const mc_piece_tree *tree, bool extend);
void mc_cursors_move_home(mc_cursor_set *cs, const mc_piece_tree *tree,
                          bool extend, bool smart);
void mc_cursors_move_end(mc_cursor_set *cs, const mc_piece_tree *tree, bool extend);

/* Ctrl+D: select word if primary empty, else add next occurrence */
bool mc_cursors_add_next_occurrence(mc_cursor_set *cs, const mc_piece_tree *tree);

/* Multi-cursor editing (applies via edit_stack as a single transaction) */
int mc_cursors_insert_text(mc_cursor_set *cs, mc_edit_stack *stack,
                           const wchar_t *text, size_t len,
                           mc_edit_kind_t kind, uint64_t timestamp_ms);
int mc_cursors_paste(mc_cursor_set *cs, mc_edit_stack *stack,
                     const wchar_t *text, size_t len, uint64_t timestamp_ms);
int mc_cursors_backspace(mc_cursor_set *cs, mc_edit_stack *stack,
                         bool by_word, uint64_t timestamp_ms);
int mc_cursors_delete_forward(mc_cursor_set *cs, mc_edit_stack *stack,
                              bool by_word, uint64_t timestamp_ms);
int mc_cursors_indent(mc_cursor_set *cs, mc_edit_stack *stack,
                      uint32_t tab_size, bool insert_spaces,
                      bool outdent, uint64_t timestamp_ms);
int mc_cursors_copy_text(const mc_cursor_set *cs, const mc_piece_tree *tree,
                         mc_wbuf *out_wbuf);

#endif /* MONAC_CORE_CURSOR_H */
