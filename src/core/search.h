#ifndef MONAC_CORE_SEARCH_H
#define MONAC_CORE_SEARCH_H

#include "cursor.h"
#include "decorations.h"
#include "edit_stack.h"
#include "piece_tree.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct mc_search_options_t {
    bool case_sensitive;
    bool whole_word;
} mc_search_options_t;

typedef struct mc_search_match_t {
    size_t start_offset;
    size_t end_offset;
    mc_range_t range;
} mc_search_match_t;

typedef struct mc_search_state {
    wchar_t *query;
    size_t query_len;
    mc_search_options_t options;
    mc_search_match_t *matches;
    size_t match_count;
    size_t match_cap;
    size_t current_index;
    bool has_current;
} mc_search_state;

void mc_search_init(mc_search_state *st);
void mc_search_destroy(mc_search_state *st);

int mc_search_update(mc_search_state *st, const mc_piece_tree *tree,
                     const wchar_t *query, size_t query_len,
                     mc_search_options_t options);

int mc_search_find_next(mc_search_state *st, size_t from_offset, bool wrap,
                        mc_search_match_t *out_match);
int mc_search_find_prev(mc_search_state *st, size_t from_offset, bool wrap,
                        mc_search_match_t *out_match);

void mc_search_apply_decorations(const mc_search_state *st, mc_dec_tree *decs,
                                 uint32_t owner_id);

int mc_search_replace_current(mc_search_state *st, mc_edit_stack *stack,
                              mc_cursor_set *cursors,
                              const wchar_t *replacement, size_t repl_len,
                              uint64_t timestamp_ms);
size_t mc_search_replace_all(mc_search_state *st, mc_edit_stack *stack,
                             mc_cursor_set *cursors,
                             const wchar_t *replacement, size_t repl_len,
                             uint64_t timestamp_ms);

#endif /* MONAC_CORE_SEARCH_H */
