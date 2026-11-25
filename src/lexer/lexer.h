#ifndef MONAC_LEXER_LEXER_H
#define MONAC_LEXER_LEXER_H

#include "../core/piece_tree.h"
#include "../core/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef enum mc_token_kind_t {
    MC_TOK_DEFAULT = 0,
    MC_TOK_JSON_KEY = 1,
    MC_TOK_JSON_STRING = 2,
    MC_TOK_JSON_NUMBER = 3,
    MC_TOK_JSON_BOOL = 4,
    MC_TOK_JSON_NULL = 5,
    MC_TOK_JSON_PUNCT = 6,
    MC_TOK_JSON_COMMENT = 7,
    MC_TOK_JSON_ERROR = 8,
    MC_TOK_KEYWORD = 9,
    MC_TOK_TYPE = 10,
    MC_TOK_FUNCTION = 11,
    MC_TOK_VARIABLE = 12,
    MC_TOK_PROPERTY = 13,
    MC_TOK_OPERATOR = 14,
    MC_TOK_PREPROC = 15,
    MC_TOK_MAX_BUILTIN = 32
} mc_token_kind_t;

typedef struct mc_token_t {
    uint32_t start_col;
    uint32_t length;
    uint32_t style_id;
} mc_token_t;

typedef struct mc_fold_candidate_t {
    mc_range_t range;
    wchar_t placeholder[16];
    bool is_inline;
} mc_fold_candidate_t;

typedef struct mc_lexer_vtable_t {
    uint32_t (*initial_state)(void *userdata);
    uint32_t (*tokenize_line)(void *userdata, uint32_t in_state,
                              const wchar_t *line, size_t len,
                              mc_token_t *out_tokens, size_t *inout_count);
    size_t (*extract_folds)(void *userdata, const mc_piece_tree *tree,
                            mc_fold_candidate_t *out_folds, size_t cap);
} mc_lexer_vtable_t;

typedef struct mc_line_tokens_t {
    uint32_t start_state;
    uint32_t end_state;
    mc_token_t *tokens;
    size_t count;
    size_t cap;
    bool valid;
} mc_line_tokens_t;

typedef struct mc_lexer_cache {
    mc_lexer_vtable_t vtable;
    void *userdata;
    bool has_lexer;
    mc_line_tokens_t *lines;
    size_t line_count;
    size_t line_cap;
    uint32_t tokenize_max_line;
    uint32_t dirty_from;
    uint32_t edited_end;
} mc_lexer_cache;

void mc_lexer_cache_init(mc_lexer_cache *cache);
void mc_lexer_cache_destroy(mc_lexer_cache *cache);
void mc_lexer_cache_set_lexer(mc_lexer_cache *cache,
                              const mc_lexer_vtable_t *vtable,
                              void *userdata);
void mc_lexer_cache_set_max_line(mc_lexer_cache *cache, uint32_t max_line_len);

void mc_lexer_cache_invalidate_all(mc_lexer_cache *cache);
void mc_lexer_cache_invalidate_lines(mc_lexer_cache *cache,
                                     uint32_t first_line,
                                     uint32_t last_edited_line,
                                     size_t new_line_count);

/* Ensures tokens are valid up through target_line (incremental convergence). */
int mc_lexer_cache_ensure_line(mc_lexer_cache *cache,
                               const mc_piece_tree *tree,
                               uint32_t target_line);

const mc_line_tokens_t *mc_lexer_cache_get_line(const mc_lexer_cache *cache,
                                                uint32_t line);

size_t mc_lexer_extract_folds(const mc_lexer_cache *cache,
                              const mc_piece_tree *tree,
                              mc_fold_candidate_t *out_folds, size_t cap);

#endif /* MONAC_LEXER_LEXER_H */
