#include "mtest.h"
#include "../src/base/arena.h"
#include "../src/base/strbuf.h"
#include "../src/base/vec.h"

#include <stdint.h>
#include <string.h>

static void test_arena_alignment_and_reset(void)
{
    mc_arena arena;
    void *first;
    void *aligned;
    mc_arena_init(&arena, 32);
    first = mc_arena_alloc(&arena, 8, 0);
    aligned = mc_arena_alloc(&arena, 12, 64);
    MT_ASSERT(first != NULL);
    MT_ASSERT(aligned != NULL);
    MT_ASSERT_EQ(0, (uintptr_t)aligned % 64u);
    mc_arena_reset(&arena);
    MT_ASSERT(mc_arena_alloc(&arena, 8, 0) != NULL);
    MT_ASSERT(mc_arena_alloc(&arena, 1, 3) == NULL);
    mc_arena_destroy(&arena);
}

static void test_pool_alloc_free_reuse(void)
{
    mc_pool pool;
    void *ptrs[32];
    int i;
    mc_pool_init(&pool, 24, 8);
    for (i = 0; i < 32; ++i) {
        ptrs[i] = mc_pool_alloc(&pool);
        MT_ASSERT(ptrs[i] != NULL);
    }
    MT_ASSERT_EQ(32, pool.live_count);
    for (i = 0; i < 16; ++i) {
        mc_pool_free(&pool, ptrs[i]);
    }
    MT_ASSERT_EQ(16, pool.live_count);
    for (i = 0; i < 16; ++i) {
        MT_ASSERT(mc_pool_alloc(&pool) != NULL);
    }
    MT_ASSERT_EQ(32, pool.live_count);
    mc_pool_clear(&pool);
    MT_ASSERT_EQ(0, pool.live_count);
    mc_pool_destroy(&pool);
}

static void test_buffers_and_vector(void)
{
    mc_strbuf text;
    mc_wbuf wide;
    int *items = NULL;
    size_t capacity = 0;
    mc_strbuf_init(&text);
    mc_wbuf_init(&wide);
    MT_ASSERT(mc_strbuf_append(&text, "hello", 5));
    MT_ASSERT(mc_strbuf_append_char(&text, '!'));
    MT_ASSERT_EQ(6, text.length);
    MT_ASSERT(strcmp(text.data, "hello!") == 0);
    MT_ASSERT(mc_wbuf_append(&wide, L"wide", 4));
    MT_ASSERT(mc_wbuf_append_char(&wide, L'!'));
    MT_ASSERT_EQ(5, wide.length);
    MT_ASSERT(wide.data[4] == L'!');
    MT_ASSERT(mc_vec_reserve((void **)&items, &capacity, sizeof(*items), 10));
    MT_ASSERT(capacity >= 10);
    items[0] = 42;
    MT_ASSERT_EQ(42, items[0]);
    free(items);
    mc_strbuf_destroy(&text);
    mc_wbuf_destroy(&wide);
}

int main(void)
{
    MT_RUN(test_arena_alignment_and_reset);
    MT_RUN(test_pool_alloc_free_reuse);
    MT_RUN(test_buffers_and_vector);
    return MT_FINISH();
}
