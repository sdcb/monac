#ifndef MONAC_CORE_PROJECTION_H
#define MONAC_CORE_PROJECTION_H

#include "../base/strbuf.h"
#include "../platform/dw_text.h"
#include "../platform/inline_object.h"
#include "fenwick.h"
#include "folding.h"
#include "line_wrap.h"
#include "piece_tree.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef enum mc_proj_seg_kind_t {
    MC_SEG_TEXT = 0,
    MC_SEG_FOLD_PLACEHOLDER = 1,
    MC_SEG_INLAY = 2
} mc_proj_seg_kind_t;

typedef struct mc_proj_segment_t {
    mc_proj_seg_kind_t kind;
    uint32_t view_col_start;
    uint32_t view_col_len;
    /* MC_SEG_TEXT */
    uint32_t model_line;
    uint32_t model_col_start;
    uint32_t model_col_len;
    /* MC_SEG_FOLD_PLACEHOLDER */
    uint32_t fold_id;
    mc_pos_t fold_start;
    mc_pos_t fold_end;
    /* MC_SEG_INLAY */
    mc_pos_t inlay_pos;
    uint32_t inlay_index;
} mc_proj_segment_t;

typedef struct mc_inlay_entry_t {
    uint32_t owner_id;
    mc_pos_t pos;
    wchar_t *text;
    uint32_t fg_color;
    uint32_t bg_color;
} mc_inlay_entry_t;

typedef struct mc_inlay_spec_t {
    mc_pos_t pos;
    const wchar_t *text;
    uint32_t fg_color;
    uint32_t bg_color;
} mc_inlay_spec_t;

typedef struct mc_logical_line_t {
    uint32_t first_model_line;
    uint32_t last_model_line;
    uint64_t version;
    bool built;
    bool has_specials;
    mc_wbuf text;
    mc_proj_segment_t *segments;
    size_t seg_count;
    size_t seg_cap;
    mc_wrap_inline_t *inlines;
    size_t inline_count;
    size_t inline_cap;
    mc_wrap_result_t wrap;
} mc_logical_line_t;

typedef struct mc_projection {
    const mc_piece_tree *tree;
    mc_folding *folding;
    mc_dw_context *dw;
    mc_inline_draw_state_t *inline_draw_state;

    mc_inlay_entry_t *inlays;
    size_t inlay_count;
    size_t inlay_cap;

    mc_logical_line_t *lines;
    size_t line_count;
    size_t line_cap;

    uint32_t *model_to_logical;
    size_t model_line_count;
    size_t model_to_logical_cap;

    mc_fenwick fenwick;

    mc_wrap_mode_t wrap_mode;
    float wrap_width;
    uint32_t long_line_chunk;
    uint64_t built_font_version;
    uint64_t next_line_version;
    bool dirty_all;
} mc_projection;

void mc_proj_init(mc_projection *proj, const mc_piece_tree *tree,
                  mc_folding *folding, mc_dw_context *dw);
void mc_proj_destroy(mc_projection *proj);
void mc_proj_set_inline_draw_state(mc_projection *proj,
                                   mc_inline_draw_state_t *draw_state);

void mc_proj_set_wrap(mc_projection *proj, mc_wrap_mode_t mode, float width);
void mc_proj_set_chunk_size(mc_projection *proj, uint32_t chunk_size);
int mc_proj_set_inlays(mc_projection *proj, uint32_t owner_id,
                       const mc_inlay_spec_t *items, size_t count);

void mc_proj_invalidate_all(mc_projection *proj);
void mc_proj_invalidate_model_line(mc_projection *proj, uint32_t model_line);
int mc_proj_ensure_valid(mc_projection *proj);

uint32_t mc_proj_view_line_count(mc_projection *proj);
const mc_logical_line_t *mc_proj_get_view_line(mc_projection *proj,
                                               uint32_t view_line,
                                               uint32_t *out_sub_line,
                                               mc_wrap_span_t *out_span);

bool mc_proj_model_to_view(mc_projection *proj, mc_pos_t model_pos,
                           mc_view_pos_t *out_view_pos);
bool mc_proj_view_to_model(mc_projection *proj, mc_view_pos_t view_pos,
                           mc_pos_t *out_model_pos, uint32_t *out_hit_fold_id);

#endif /* MONAC_CORE_PROJECTION_H */
