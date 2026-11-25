#include "lexer.h"

#include "../base/strbuf.h"
#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>

void mc_lexer_cache_init(mc_lexer_cache *cache)
{
    if (cache == NULL) return;
    memset(cache, 0, sizeof(*cache));
    cache->tokenize_max_line = 20000u;
    cache->dirty_from = 0;
    cache->edited_end = UINT32_MAX;
}

void mc_lexer_cache_destroy(mc_lexer_cache *cache)
{
    size_t i;
    if (cache == NULL) return;
    for (i = 0; i < cache->line_cap; ++i) {
        free(cache->lines[i].tokens);
    }
    free(cache->lines);
    memset(cache, 0, sizeof(*cache));
}

void mc_lexer_cache_set_lexer(mc_lexer_cache *cache,
                              const mc_lexer_vtable_t *vtable,
                              void *userdata)
{
    if (cache == NULL) return;
    if (vtable != NULL && vtable->tokenize_line != NULL) {
        cache->vtable = *vtable;
        cache->userdata = userdata;
        cache->has_lexer = true;
    } else {
        memset(&cache->vtable, 0, sizeof(cache->vtable));
        cache->userdata = NULL;
        cache->has_lexer = false;
    }
    mc_lexer_cache_invalidate_all(cache);
}

void mc_lexer_cache_set_max_line(mc_lexer_cache *cache, uint32_t max_line_len)
{
    if (cache == NULL) return;
    cache->tokenize_max_line = max_line_len;
    mc_lexer_cache_invalidate_all(cache);
}

void mc_lexer_cache_invalidate_all(mc_lexer_cache *cache)
{
    size_t i;
    if (cache == NULL) return;
    for (i = 0; i < cache->line_count; ++i) {
        cache->lines[i].valid = false;
        cache->lines[i].count = 0;
    }
    cache->dirty_from = 0;
    cache->edited_end = UINT32_MAX;
}

static int mc_lexer_ensure_cap(mc_lexer_cache *cache, size_t needed)
{
    size_t old_cap = cache->line_cap;
    if (needed <= old_cap) return 1;
    if (!mc_vec_reserve((void **)&cache->lines, &cache->line_cap,
                        sizeof(mc_line_tokens_t), needed)) {
        return 0;
    }
    memset(cache->lines + old_cap, 0,
           (cache->line_cap - old_cap) * sizeof(mc_line_tokens_t));
    return 1;
}

void mc_lexer_cache_invalidate_lines(mc_lexer_cache *cache,
                                     uint32_t first_line,
                                     uint32_t last_edited_line,
                                     size_t new_line_count)
{
    size_t i;
    if (cache == NULL) return;
    if (new_line_count != cache->line_count) {
        /* Line count changed: invalidate from first_line onward */
        if (mc_lexer_ensure_cap(cache, new_line_count)) {
            for (i = first_line; i < new_line_count; ++i) {
                cache->lines[i].valid = false;
            }
            cache->line_count = new_line_count;
        }
        if (first_line < cache->dirty_from) cache->dirty_from = first_line;
        cache->edited_end = UINT32_MAX;
        return;
    }
    for (i = first_line; i <= last_edited_line && i < cache->line_count; ++i) {
        cache->lines[i].valid = false;
    }
    if (first_line < cache->dirty_from) cache->dirty_from = first_line;
    if (cache->edited_end != UINT32_MAX && last_edited_line > cache->edited_end) {
        cache->edited_end = last_edited_line;
    }
}

int mc_lexer_cache_ensure_line(mc_lexer_cache *cache,
                               const mc_piece_tree *tree,
                               uint32_t target_line)
{
    size_t total_lines;
    uint32_t init_state;
    uint32_t line;
    mc_wbuf scratch;

    if (cache == NULL || tree == NULL) return 0;
    total_lines = mc_pt_line_count(tree);
    if (total_lines == 0) return 1;
    if (target_line >= total_lines) target_line = (uint32_t)(total_lines - 1);

    if (!cache->has_lexer) {
        cache->line_count = total_lines;
        cache->dirty_from = (uint32_t)total_lines;
        return 1;
    }

    if (total_lines != cache->line_count) {
        size_t old_n = cache->line_count;
        if (!mc_lexer_ensure_cap(cache, total_lines)) return 0;
        if (total_lines > old_n) {
            size_t i;
            for (i = old_n; i < total_lines; ++i) cache->lines[i].valid = false;
        }
        cache->line_count = total_lines;
        if (cache->dirty_from > old_n) cache->dirty_from = (uint32_t)old_n;
    }

    if (cache->dirty_from > target_line && cache->lines[target_line].valid) {
        return 1;
    }

    init_state = cache->vtable.initial_state
                     ? cache->vtable.initial_state(cache->userdata)
                     : 0u;

    mc_wbuf_init(&scratch);
    line = cache->dirty_from;
    if (line > target_line) line = 0;

    for (; line <= target_line; ++line) {
        mc_line_tokens_t *lt = &cache->lines[line];
        uint32_t in_state = (line == 0) ? init_state : cache->lines[line - 1].end_state;
        uint32_t old_end = lt->end_state;
        bool was_valid = lt->valid;
        uint32_t out_state;

        if (was_valid && lt->start_state == in_state && line > cache->edited_end) {
            /* Already valid with identical start state: converged! */
            cache->dirty_from = (uint32_t)total_lines;
            cache->edited_end = 0;
            mc_wbuf_destroy(&scratch);
            return 1;
        }

        if (!mc_pt_get_line(tree, line, &scratch)) {
            mc_wbuf_destroy(&scratch);
            return 0;
        }

        lt->start_state = in_state;
        if (cache->tokenize_max_line > 0 && scratch.length > cache->tokenize_max_line) {
            lt->count = 0;
            out_state = init_state;
        } else {
            size_t cap = lt->cap > 0 ? lt->cap : 64;
            if (!mc_vec_reserve((void **)&lt->tokens, &lt->cap,
                                sizeof(mc_token_t), cap)) {
                mc_wbuf_destroy(&scratch);
                return 0;
            }
            {
                size_t inout = lt->cap;
                out_state = cache->vtable.tokenize_line(
                    cache->userdata, in_state,
                    scratch.data ? scratch.data : L"", scratch.length,
                    lt->tokens, &inout);
                if (inout > lt->cap) {
                    if (!mc_vec_reserve((void **)&lt->tokens, &lt->cap,
                                        sizeof(mc_token_t), inout)) {
                        mc_wbuf_destroy(&scratch);
                        return 0;
                    }
                    inout = lt->cap;
                    out_state = cache->vtable.tokenize_line(
                        cache->userdata, in_state,
                        scratch.data ? scratch.data : L"", scratch.length,
                        lt->tokens, &inout);
                }
                lt->count = inout <= lt->cap ? inout : lt->cap;
            }
        }
        lt->end_state = out_state;
        lt->valid = true;

        /* Check early state convergence past edited region */
        if (was_valid && line >= cache->edited_end && out_state == old_end) {
            if (line + 1 < total_lines && cache->lines[line + 1].valid &&
                cache->lines[line + 1].start_state == out_state) {
                cache->dirty_from = (uint32_t)total_lines;
                cache->edited_end = 0;
                mc_wbuf_destroy(&scratch);
                return 1;
            }
        }
    }

    cache->dirty_from = target_line + 1;
    if (cache->dirty_from >= total_lines) {
        cache->edited_end = 0;
    }
    mc_wbuf_destroy(&scratch);
    return 1;
}

const mc_line_tokens_t *mc_lexer_cache_get_line(const mc_lexer_cache *cache,
                                                uint32_t line)
{
    if (cache == NULL || !cache->has_lexer || line >= cache->line_count)
        return NULL;
    if (!cache->lines[line].valid) return NULL;
    return &cache->lines[line];
}

size_t mc_lexer_extract_folds(const mc_lexer_cache *cache,
                              const mc_piece_tree *tree,
                              mc_fold_candidate_t *out_folds, size_t cap)
{
    if (cache == NULL || !cache->has_lexer || cache->vtable.extract_folds == NULL ||
        tree == NULL) {
        return 0;
    }
    return cache->vtable.extract_folds(cache->userdata, tree, out_folds, cap);
}
