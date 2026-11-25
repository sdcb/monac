#include "arena.h"

#include <stdint.h>
#include <stdlib.h>

struct mc_arena_block {
    struct mc_arena_block *next;
    size_t capacity;
    size_t used;
    unsigned char data[];
};

static int mc_arena_is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static mc_arena_block *mc_arena_new_block(size_t capacity)
{
    mc_arena_block *block;
    if (capacity > SIZE_MAX - sizeof(*block)) return NULL;
    block = (mc_arena_block *)malloc(sizeof(*block) + capacity);
    if (block != NULL) {
        block->next = NULL;
        block->capacity = capacity;
        block->used = 0;
    }
    return block;
}

void mc_arena_init(mc_arena *arena, size_t block_size)
{
    if (arena != NULL) {
        arena->head = NULL;
        arena->current = NULL;
        arena->block_size = block_size == 0 ? 4096 : block_size;
    }
}

void mc_arena_destroy(mc_arena *arena)
{
    mc_arena_block *block;
    if (arena == NULL) return;
    block = arena->head;
    while (block != NULL) {
        mc_arena_block *next = block->next;
        free(block);
        block = next;
    }
    mc_arena_init(arena, 0);
}

void mc_arena_reset(mc_arena *arena)
{
    mc_arena_block *block;
    if (arena == NULL) return;
    for (block = arena->head; block != NULL; block = block->next)
        block->used = 0;
    arena->current = arena->head;
}

void *mc_arena_alloc(mc_arena *arena, size_t size, size_t alignment)
{
    mc_arena_block *block;
    if (arena == NULL || size == 0) return NULL;
    if (alignment == 0) {
#if defined(_MSC_VER)
        alignment = __alignof(long double);
#else
        alignment = _Alignof(max_align_t);
#endif
    }
    if (!mc_arena_is_power_of_two(alignment)) return NULL;
    for (block = arena->current; block != NULL; block = block->next) {
        uintptr_t address = (uintptr_t)(block->data + block->used);
        uintptr_t aligned;
        size_t padding;
        if (address > UINTPTR_MAX - (alignment - 1)) return NULL;
        aligned = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
        padding = (size_t)(aligned - address);
        if (padding <= block->capacity - block->used &&
            size <= block->capacity - block->used - padding) {
            block->used += padding + size;
            arena->current = block;
            return (void *)aligned;
        }
    }
    {
        size_t required;
        size_t capacity = arena->block_size;
        if (size > SIZE_MAX - (alignment - 1)) return NULL;
        required = size + alignment - 1;
        if (capacity < required) capacity = required;
        block = mc_arena_new_block(capacity);
        if (block == NULL) return NULL;
        if (arena->head == NULL) {
            arena->head = block;
        } else {
            mc_arena_block *tail = arena->head;
            while (tail->next != NULL) tail = tail->next;
            tail->next = block;
        }
        arena->current = block;
        return mc_arena_alloc(arena, size, alignment);
    }
}

struct mc_pool_free_node {
    struct mc_pool_free_node *next;
};

struct mc_pool_slab {
    struct mc_pool_slab *next;
    size_t used;
    unsigned char data[];
};

void mc_pool_init(mc_pool *pool, size_t item_size, size_t items_per_slab)
{
    size_t align;
    if (pool == NULL) return;
    align = sizeof(void *);
    if (item_size < sizeof(mc_pool_free_node)) item_size = sizeof(mc_pool_free_node);
    item_size = (item_size + align - 1) & ~(align - 1);
    pool->slabs = NULL;
    pool->free_list = NULL;
    pool->item_size = item_size;
    pool->items_per_slab = items_per_slab == 0 ? 128 : items_per_slab;
    pool->live_count = 0;
}

void mc_pool_destroy(mc_pool *pool)
{
    mc_pool_slab *slab;
    if (pool == NULL) return;
    slab = pool->slabs;
    while (slab != NULL) {
        mc_pool_slab *next = slab->next;
        free(slab);
        slab = next;
    }
    pool->slabs = NULL;
    pool->free_list = NULL;
    pool->live_count = 0;
}

void mc_pool_clear(mc_pool *pool)
{
    mc_pool_slab *slab;
    if (pool == NULL) return;
    for (slab = pool->slabs; slab != NULL; slab = slab->next) {
        slab->used = 0;
    }
    pool->free_list = NULL;
    pool->live_count = 0;
}

void *mc_pool_alloc(mc_pool *pool)
{
    mc_pool_slab *slab;
    if (pool == NULL || pool->item_size == 0) return NULL;
    if (pool->free_list != NULL) {
        mc_pool_free_node *node = pool->free_list;
        pool->free_list = node->next;
        ++pool->live_count;
        return (void *)node;
    }
    for (slab = pool->slabs; slab != NULL; slab = slab->next) {
        if (slab->used < pool->items_per_slab) {
            void *ptr = slab->data + slab->used * pool->item_size;
            ++slab->used;
            ++pool->live_count;
            return ptr;
        }
    }
    if (pool->items_per_slab > (SIZE_MAX - sizeof(mc_pool_slab)) / pool->item_size)
        return NULL;
    slab = (mc_pool_slab *)malloc(sizeof(mc_pool_slab) +
                                  pool->items_per_slab * pool->item_size);
    if (slab == NULL) return NULL;
    slab->next = pool->slabs;
    slab->used = 1;
    pool->slabs = slab;
    ++pool->live_count;
    return (void *)slab->data;
}

void mc_pool_free(mc_pool *pool, void *ptr)
{
    mc_pool_free_node *node;
    if (pool == NULL || ptr == NULL) return;
    node = (mc_pool_free_node *)ptr;
    node->next = pool->free_list;
    pool->free_list = node;
    if (pool->live_count > 0) --pool->live_count;
}
