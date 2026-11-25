#ifndef MONAC_CORE_DECORATIONS_H
#define MONAC_CORE_DECORATIONS_H

#include "../base/arena.h"
#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

enum {
    MC_OWNER_FOLDING = 1,
    MC_OWNER_SEARCH = 2,
    MC_OWNER_DIAGNOSTICS = 3,
    MC_OWNER_USER_BASE = 100
};

typedef enum mc_stickiness_t {
    MC_STICKY_ALWAYS_GROWS = 0,
    MC_STICKY_NEVER_GROWS = 1,
    MC_STICKY_GROWS_ONLY_BEFORE = 2,
    MC_STICKY_GROWS_ONLY_AFTER = 3
} mc_stickiness_t;

typedef enum mc_underline_style_t {
    MC_UNDERLINE_NONE = 0,
    MC_UNDERLINE_SOLID = 1,
    MC_UNDERLINE_SQUIGGLY_ERROR = 2,
    MC_UNDERLINE_SQUIGGLY_WARNING = 3,
    MC_UNDERLINE_SQUIGGLY_INFO = 4
} mc_underline_style_t;

typedef struct mc_dec_style_t {
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t underline_color;
    mc_underline_style_t underline_style;
    uint32_t gutter_color;
    uint8_t gutter_icon;
    const wchar_t *hover_text;
} mc_dec_style_t;

typedef struct mc_dec_spec_t {
    size_t start;
    size_t end;
    mc_stickiness_t stickiness;
    bool collapse_on_delete;
    mc_dec_style_t style;
} mc_dec_spec_t;

typedef struct mc_dec_node {
    struct mc_dec_node *parent;
    struct mc_dec_node *left;
    struct mc_dec_node *right;
    int64_t start;
    int64_t end;
    int64_t min_start;
    int64_t max_end;
    int64_t lazy;
    uint32_t id;
    uint32_t owner_id;
    uint8_t color;
    mc_stickiness_t stickiness;
    bool collapse_on_delete;
    mc_dec_style_t style;
    wchar_t *owned_hover;
} mc_dec_node;

typedef struct mc_dec_snapshot_t {
    uint32_t id;
    uint32_t owner_id;
    size_t start;
    size_t end;
    mc_stickiness_t stickiness;
    bool collapse_on_delete;
    mc_dec_style_t style;
} mc_dec_snapshot_t;

typedef void (*mc_dec_deleted_cb)(void *userdata, uint32_t id, uint32_t owner_id);

typedef struct mc_dec_tree {
    mc_pool node_pool;
    mc_dec_node nil;
    mc_dec_node *root;
    uint32_t next_id;
    size_t count;
    mc_dec_deleted_cb on_deleted;
    void *on_deleted_ud;
} mc_dec_tree;

void mc_dec_init(mc_dec_tree *tree);
void mc_dec_destroy(mc_dec_tree *tree);
void mc_dec_clear(mc_dec_tree *tree);
void mc_dec_set_deleted_cb(mc_dec_tree *tree, mc_dec_deleted_cb cb, void *userdata);

uint32_t mc_dec_add(mc_dec_tree *tree, uint32_t owner_id, const mc_dec_spec_t *spec);
int mc_dec_remove(mc_dec_tree *tree, uint32_t id);
void mc_dec_remove_by_owner(mc_dec_tree *tree, uint32_t owner_id);
int mc_dec_replace_owner(mc_dec_tree *tree, uint32_t owner_id,
                         const mc_dec_spec_t *specs, size_t count);

int mc_dec_get(mc_dec_tree *tree, uint32_t id, mc_dec_snapshot_t *out);
size_t mc_dec_query(mc_dec_tree *tree, size_t start, size_t end,
                    mc_dec_snapshot_t *out, size_t cap);
size_t mc_dec_query_owner(mc_dec_tree *tree, uint32_t owner_id,
                          size_t start, size_t end,
                          mc_dec_snapshot_t *out, size_t cap);

void mc_dec_accept_replace(mc_dec_tree *tree, size_t offset,
                           size_t deleted_len, size_t inserted_len);

size_t mc_dec_count(const mc_dec_tree *tree);
int mc_dec_verify_invariants(mc_dec_tree *tree);

#endif /* MONAC_CORE_DECORATIONS_H */
