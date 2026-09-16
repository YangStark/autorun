/* Exercise pool exhaustion, fragmented page reuse and live-allocation isolation. */
#include <assert.h>
#include <stdio.h>
#include "../../dlls/ntdll/unix/horizon_pool.h"

struct object { void *link; unsigned long long value[6]; };
static struct object storage[4];
static struct horizon_object_pool objects = { storage, NULL, sizeof(storage[0]), 4, 0 };
static struct horizon_page_pool pool;
static unsigned int random_state = 42;
static unsigned int random_next(void) { return random_state = random_state * 1664525 + 1013904223; }

int main(void)
{
    struct object *items[5];
    void *whole[HORIZON_POOL_ARENAS];
    struct { unsigned char *ptr; size_t size; unsigned char tag; } live[128] = {0};
    unsigned int i, j, round;
    size_t bytes;
    for (i = 0; i < 5; i++)
    {
        items[i] = horizon_object_alloc(&objects);
        assert(items[i]);
        items[i]->value[0] = 123;
    }
    assert((uintptr_t)items[4] < (uintptr_t)storage || (uintptr_t)items[4] >= (uintptr_t)(storage + 4));
    horizon_object_free(&objects, items[2]);
    assert(horizon_object_alloc(&objects) == items[2] && !items[2]->value[0]);
    for (i = 0; i < 5; i++) horizon_object_free(&objects, items[i]);
    assert(!horizon_pages_alloc(&pool, 0));
    assert(!horizon_pages_alloc(&pool, 4097));
    assert(!horizon_pages_alloc(&pool, (HORIZON_POOL_PAGES + 1) * HORIZON_POOL_PAGE));
    bytes = HORIZON_POOL_ARENA;
    for (i = 0; i < HORIZON_POOL_ARENAS; i++)
    {
        whole[i] = horizon_pages_alloc(&pool, bytes);
        /* One arena is one kernel memory block, and shares it with nothing:
         * aliasing a page of it unmaps whatever else the block holds. */
        assert(whole[i] && !((uintptr_t)whole[i] % HORIZON_POOL_ARENA));
        ((unsigned char *)whole[i])[bytes - 1] = (unsigned char)(i + 1);
    }
    assert(!horizon_pages_alloc(&pool, HORIZON_POOL_PAGE));
    /* A full pool, and anything too large for an arena, take blocks of their
     * own rather than pages the general heap shares with other allocations. */
    {
        void *one = horizon_pages_alloc_any(&pool, HORIZON_POOL_PAGE);
        void *large = horizon_pages_alloc_any(&pool, HORIZON_POOL_ARENA + HORIZON_POOL_PAGE);
        assert(one && !((uintptr_t)one % HORIZON_POOL_ARENA));
        assert(large && !((uintptr_t)large % HORIZON_POOL_ARENA));
        assert(!horizon_pages_free(&pool, one, HORIZON_POOL_PAGE));
        assert(pool.blocks == 2 && !pool.shared);
        free(one);
        free(large);
    }
    /* Release and reuse a middle arena while every other arena remains live. */
    assert(horizon_pages_free(&pool, whole[7], bytes));
    assert(horizon_pages_alloc(&pool, bytes) == whole[7]);
    for (i = 0; i < HORIZON_POOL_ARENAS; i++)
    {
        assert(((unsigned char *)whole[i])[bytes - 1] == (unsigned char)(i + 1));
        assert(horizon_pages_free(&pool, whole[i], bytes));
    }
    for (round = 0; round < 20000; round++)
    {
        i = (random_next() >> 8) % 128;
        if (live[i].ptr)
        {
            for (j = 0; j < live[i].size; j += HORIZON_POOL_PAGE)
                assert(live[i].ptr[j] == live[i].tag);
            assert(horizon_pages_free(&pool, live[i].ptr, live[i].size));
            live[i].ptr = NULL;
        }
        else
        {
            live[i].size = (1 + (random_next() >> 8) % 128) * HORIZON_POOL_PAGE;
            live[i].ptr = horizon_pages_alloc(&pool, live[i].size);
            if (!live[i].ptr) continue; /* bounded pool; production falls back */
            assert(!((uintptr_t)live[i].ptr % HORIZON_POOL_PAGE));
            for (j = 0; j < 128; j++) if (j != i && live[j].ptr)
                assert((uintptr_t)live[i].ptr + live[i].size <= (uintptr_t)live[j].ptr ||
                       (uintptr_t)live[j].ptr + live[j].size <= (uintptr_t)live[i].ptr);
            live[i].tag = 1 + i;
            memset(live[i].ptr, live[i].tag, live[i].size);
        }
    }
    for (i = 0; i < 128; i++) if (live[i].ptr)
        assert(horizon_pages_free(&pool, live[i].ptr, live[i].size));
    for (i = 0; i < HORIZON_POOL_ARENAS; i++)
    {
        assert(pool.arenas[i].free_pages == HORIZON_POOL_PAGES);
        for (j = 0; j < HORIZON_POOL_PAGES; j++) assert(!pool.arenas[i].used[j]);
        free(pool.arenas[i].memory);
    }
    puts("Mapping pools: descriptor fallback, reset, capacity, alignment and 20000 fragmented allocation cycles passed");
    return 0;
}
