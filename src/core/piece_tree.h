#ifndef MONAC_CORE_PIECE_TREE_H
#define MONAC_CORE_PIECE_TREE_H

#include "../base/arena.h"
#include "../base/strbuf.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct mc_pt_node mc_pt_node;

typedef struct mc_pt_buffer {
    wchar_t *data;
    size_t len;
    size_t cap;
    size_t *line_starts;
    size_t line_starts_len;
    size_t line_starts_cap;
} mc_pt_buffer;

struct mc_pt_node {
    struct mc_pt_node *parent;
    struct mc_pt_node *left;
    struct mc_pt_node *right;
    size_t start;
    size_t length;
    size_t lf_count;
    size_t size_left;
    size_t lf_left;
    uint8_t buf_id;
    uint8_t color;
};

typedef struct mc_piece_tree {
    mc_pt_buffer original;
    mc_pt_buffer added;
    mc_pool node_pool;
    mc_pt_node nil;
    mc_pt_node *root;
    size_t total_len;
    size_t total_lf;
    mc_eol_mode_t eol_mode;
} mc_piece_tree;

void mc_pt_init(mc_piece_tree *tree);
void mc_pt_destroy(mc_piece_tree *tree);

int mc_pt_set_text(mc_piece_tree *tree, const wchar_t *text, size_t length);
int mc_pt_insert(mc_piece_tree *tree, size_t offset,
                 const wchar_t *text, size_t length);
int mc_pt_insert_ex(mc_piece_tree *tree, size_t offset,
                    const wchar_t *text, size_t length,
                    size_t *out_normalized_len);
int mc_pt_delete(mc_piece_tree *tree, size_t offset, size_t length);

mc_eol_mode_t mc_pt_get_eol_mode(const mc_piece_tree *tree);
void mc_pt_set_eol_mode(mc_piece_tree *tree, mc_eol_mode_t mode);

size_t mc_pt_length(const mc_piece_tree *tree);
size_t mc_pt_line_count(const mc_piece_tree *tree);
size_t mc_pt_line_start_offset(const mc_piece_tree *tree, uint32_t line);
size_t mc_pt_line_length(const mc_piece_tree *tree, uint32_t line);

int mc_pt_offset_to_pos(const mc_piece_tree *tree, size_t offset, mc_pos_t *out_pos);
int mc_pt_pos_to_offset(const mc_piece_tree *tree, mc_pos_t pos, size_t *out_offset);

int mc_pt_get_line(const mc_piece_tree *tree, uint32_t line, mc_wbuf *out_wbuf);
size_t mc_pt_get_line_raw(const mc_piece_tree *tree, uint32_t line,
                          wchar_t *output, size_t capacity);
int mc_pt_get_range(const mc_piece_tree *tree, size_t offset, size_t length,
                    mc_wbuf *out_wbuf);
size_t mc_pt_get_text(const mc_piece_tree *tree, wchar_t *output, size_t capacity);
size_t mc_pt_get_text_raw(const mc_piece_tree *tree, wchar_t *output, size_t capacity);
int mc_pt_get_char(const mc_piece_tree *tree, size_t offset, wchar_t *out_char);

int mc_pt_verify_invariants(const mc_piece_tree *tree);

#endif /* MONAC_CORE_PIECE_TREE_H */
