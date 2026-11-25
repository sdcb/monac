#ifndef MONAC_BASE_ARENA_H
#define MONAC_BASE_ARENA_H

#include <stddef.h>

typedef struct mc_arena_block mc_arena_block;

typedef struct mc_arena {
    mc_arena_block *head;
    mc_arena_block *current;
    size_t block_size;
} mc_arena;

void mc_arena_init(mc_arena *arena, size_t block_size);
void mc_arena_destroy(mc_arena *arena);
void mc_arena_reset(mc_arena *arena);
void *mc_arena_alloc(mc_arena *arena, size_t size, size_t alignment);

typedef struct mc_pool_slab mc_pool_slab;
typedef struct mc_pool_free_node mc_pool_free_node;

typedef struct mc_pool {
    mc_pool_slab *slabs;
    mc_pool_free_node *free_list;
    size_t item_size;
    size_t items_per_slab;
    size_t live_count;
} mc_pool;

void mc_pool_init(mc_pool *pool, size_t item_size, size_t items_per_slab);
void mc_pool_destroy(mc_pool *pool);
void mc_pool_clear(mc_pool *pool);
void *mc_pool_alloc(mc_pool *pool);
void mc_pool_free(mc_pool *pool, void *ptr);

#endif
