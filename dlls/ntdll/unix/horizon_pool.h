/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * Bounded storage for mapping objects and page-aligned backing memory.
 * The caller holds mapping_mutex. No live mapped pages may be returned. */
#ifndef HORIZON_POOL_H
#define HORIZON_POOL_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct horizon_object_pool
{
    void *storage, *free;
    size_t size, count, used;
};

static inline void *horizon_object_alloc( struct horizon_object_pool *pool )
{
    void *ptr;
    if ((ptr = pool->free)) memcpy( &pool->free, ptr, sizeof(pool->free) );
    else if (pool->used < pool->count) ptr = (char *)pool->storage + pool->size * pool->used++;
    else return calloc( 1, pool->size );
    memset( ptr, 0, pool->size );
    return ptr;
}

static inline void horizon_object_free( struct horizon_object_pool *pool, void *ptr )
{
    uintptr_t offset;
    if (!ptr) return;
    offset = (uintptr_t)ptr - (uintptr_t)pool->storage;
    if (offset < pool->size * pool->count && !(offset % pool->size))
    {
        memcpy( ptr, &pool->free, sizeof(pool->free) );
        pool->free = ptr;
    }
    else free( ptr );
}

#define HORIZON_POOL_PAGE 4096
#define HORIZON_POOL_PAGES 512
#define HORIZON_POOL_ARENAS 32  /* at most 64 MiB retained, allocated on demand */
struct horizon_page_arena
{
    unsigned char *memory;
    unsigned short free_pages;
    unsigned char used[HORIZON_POOL_PAGES];
};
struct horizon_page_pool
{
    struct horizon_page_arena arenas[HORIZON_POOL_ARENAS];
    unsigned long long hits, misses;
};

/* Large requests and a full pool fall back to the ordinary allocator. Each
 * arena is independent of the general heap's small allocation metadata. */
static inline void *horizon_pages_alloc( struct horizon_page_pool *pool, size_t size )
{
    size_t pages = size / HORIZON_POOL_PAGE;
    unsigned int i, j, run;
    if (!size || size % HORIZON_POOL_PAGE || pages > HORIZON_POOL_PAGES) return NULL;
    for (i = 0; i < HORIZON_POOL_ARENAS; i++)
    {
        struct horizon_page_arena *arena = &pool->arenas[i];
        if (!arena->memory)
        {
            arena->memory = aligned_alloc( HORIZON_POOL_PAGE, HORIZON_POOL_PAGES * HORIZON_POOL_PAGE );
            if (!arena->memory) return NULL;
            arena->free_pages = HORIZON_POOL_PAGES;
            pool->misses++;
        }
        if (arena->free_pages < pages) continue;
        for (j = run = 0; j < HORIZON_POOL_PAGES; j++)
        {
            run = arena->used[j] ? 0 : run + 1;
            if (run == pages)
            {
                unsigned int first = j + 1 - run;
                memset( arena->used + first, 1, pages );
                arena->free_pages -= pages;
                pool->hits++;
                return arena->memory + first * HORIZON_POOL_PAGE;
            }
        }
    }
    return NULL;
}

/* Returns zero for a direct allocation, which the caller must free normally. */
static inline int horizon_pages_free( struct horizon_page_pool *pool, void *ptr, size_t size )
{
    unsigned int i;
    for (i = 0; i < HORIZON_POOL_ARENAS; i++)
    {
        struct horizon_page_arena *arena = &pool->arenas[i];
        uintptr_t offset = (uintptr_t)ptr - (uintptr_t)arena->memory;
        if (arena->memory && offset < HORIZON_POOL_PAGES * HORIZON_POOL_PAGE)
        {
            size_t pages = size / HORIZON_POOL_PAGE;
            memset( arena->used + offset / HORIZON_POOL_PAGE, 0, pages );
            arena->free_pages += pages;
            return 1;
        }
    }
    return 0;
}
#endif
