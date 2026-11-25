#ifndef MONAC_CORE_FOLDING_H
#define MONAC_CORE_FOLDING_H

#include "decorations.h"
#include "piece_tree.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef void (*mc_fold_toggled_cb)(void *userdata, uint32_t fold_id, bool collapsed);

typedef struct mc_fold_entry_t {
    uint32_t dec_id;
    bool collapsed;
    wchar_t *placeholder;
} mc_fold_entry_t;

typedef struct mc_fold_info_t {
    uint32_t id;
    size_t start_offset;
    size_t end_offset;
    mc_range_t range;
    bool collapsed;
    const wchar_t *placeholder;
} mc_fold_info_t;

typedef struct mc_folding {
    mc_dec_tree *decorations;
    mc_fold_entry_t *entries;
    size_t count;
    size_t capacity;
    mc_fold_toggled_cb on_toggled;
    void *on_toggled_ud;
} mc_folding;

void mc_folding_init(mc_folding *f, mc_dec_tree *decorations);
void mc_folding_destroy(mc_folding *f);
void mc_folding_clear(mc_folding *f);
void mc_folding_set_toggled_cb(mc_folding *f, mc_fold_toggled_cb cb, void *userdata);

uint32_t mc_folding_add(mc_folding *f, const mc_piece_tree *tree,
                        mc_range_t range, const wchar_t *placeholder,
                        bool collapsed);
uint32_t mc_folding_add_offsets(mc_folding *f, size_t start_offset, size_t end_offset,
                                const wchar_t *placeholder, bool collapsed);
int mc_folding_remove(mc_folding *f, uint32_t fold_id);
int mc_folding_set_collapsed(mc_folding *f, uint32_t fold_id, bool collapsed);
bool mc_folding_toggle_at(mc_folding *f, const mc_piece_tree *tree, mc_pos_t pos);
void mc_folding_fold_all(mc_folding *f, bool collapsed);

bool mc_folding_reveal_offset(mc_folding *f, size_t offset);
bool mc_folding_reveal_range(mc_folding *f, size_t start_offset, size_t end_offset);

/* Call BEFORE mc_dec_accept_replace so boundary deletion & auto-unfold use pre-edit offsets. */
void mc_folding_will_replace(mc_folding *f, size_t offset,
                             size_t deleted_len, size_t inserted_len);
/* Call AFTER mc_dec_accept_replace to purge any folds whose decorations collapsed/disappeared. */
void mc_folding_did_replace(mc_folding *f);

size_t mc_folding_count(const mc_folding *f);
int mc_folding_get(mc_folding *f, const mc_piece_tree *tree,
                   uint32_t fold_id, mc_fold_info_t *out);
size_t mc_folding_list(mc_folding *f, const mc_piece_tree *tree,
                       mc_fold_info_t *out, size_t cap);

/* Returns top-level (non-nested inside another collapsed fold) collapsed folds sorted by start_offset. */
size_t mc_folding_list_top_collapsed(mc_folding *f, const mc_piece_tree *tree,
                                     mc_fold_info_t *out, size_t cap);

#endif /* MONAC_CORE_FOLDING_H */
