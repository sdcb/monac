#ifndef MONAC_CORE_DOCUMENT_H
#define MONAC_CORE_DOCUMENT_H

#include "../platform/dw_text.h"
#include "cursor.h"
#include "decorations.h"
#include "edit_stack.h"
#include "folding.h"
#include "piece_tree.h"
#include "projection.h"
#include "search.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*mc_doc_text_changed_cb)(void *userdata, uint64_t version_id,
                                       uint32_t first_dirty_line);
typedef void (*mc_doc_cursor_changed_cb)(void *userdata);

typedef struct mc_document {
    mc_piece_tree tree;
    mc_edit_stack edit_stack;
    mc_dec_tree decorations;
    mc_folding folding;
    mc_cursor_set cursors;
    mc_search_state search;
    mc_dw_context dw;
    mc_projection projection;

    uint32_t tab_size;
    bool insert_spaces;
    uint32_t tokenize_max_line;
    uint32_t dirty_first_line;

    mc_doc_text_changed_cb on_text_changed;
    void *on_text_changed_ud;
    mc_doc_cursor_changed_cb on_cursor_changed;
    void *on_cursor_changed_ud;
} mc_document;

int mc_doc_init(mc_document *doc);
void mc_doc_destroy(mc_document *doc);

int mc_doc_set_text(mc_document *doc, const wchar_t *text, size_t len);
size_t mc_doc_get_text(const mc_document *doc, wchar_t *buf, size_t cap);
uint64_t mc_doc_version(const mc_document *doc);

int mc_doc_apply_range_edits(mc_document *doc,
                             const mc_range_t *ranges,
                             const wchar_t *const *texts,
                             const size_t *lens,
                             size_t count,
                             uint64_t timestamp_ms);

int mc_doc_undo(mc_document *doc);
int mc_doc_redo(mc_document *doc);

void mc_doc_set_selections(mc_document *doc, const mc_selection_t *sels, size_t count);
void mc_doc_reveal_cursors(mc_document *doc);

/* Visual-line aware vertical navigation (handles soft wrap & folds) */
void mc_doc_move_up(mc_document *doc, bool extend);
void mc_doc_move_down(mc_document *doc, bool extend);

uint32_t mc_doc_fold_add(mc_document *doc, mc_range_t range,
                         const wchar_t *placeholder, bool collapsed);
void mc_doc_fold_set_collapsed(mc_document *doc, uint32_t fold_id, bool collapsed);
bool mc_doc_fold_toggle_at(mc_document *doc, mc_pos_t pos);
void mc_doc_fold_all(mc_document *doc, bool collapsed);
void mc_doc_fold_clear(mc_document *doc);

#endif /* MONAC_CORE_DOCUMENT_H */
