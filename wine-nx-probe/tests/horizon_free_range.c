/* Host test for the kernel free-range walk (dlls/ntdll/unix/horizon_free_range.h). */
#include <assert.h>
#include <stdio.h>

#include "../../dlls/ntdll/unix/horizon_free_range.h"

/* The hardware run of nx-wow64-dynarec-61: a libnx thread stack mirror at
 * 0x4af2000-0x4bf2000 in the small map, which Wine reserved over at 0x4ae0000. */
static const struct horizon_region layout[] =
{
    { 0x0,         0x200000,              0 },
    { 0x200000,    0x48f2000 - 0x200000,  0 },
    { 0x48f2000,   0x100000,              3 },  /* another stack */
    { 0x49f2000,   0x100000,              0 },
    { 0x4af2000,   0x100000,              3 },  /* the stack of build 61 */
    { 0x4bf2000,   0x4e400000 - 0x4bf2000, 0 },
    { 0x4e400000,  0x80000000,            5 },  /* heap */
    { 0xce400000,  0x100000000ull - 0xce400000, 0 },
    { 0x100000000ull, 0ull - 0x100000000ull, 0 },  /* to the top: its end wraps */
};
static unsigned int queries;

static int query( void *context, unsigned long long addr, struct horizon_region *region )
{
    unsigned int i;

    (void)context;
    queries++;
    for (i = 0; i < sizeof(layout) / sizeof(layout[0]); i++)
    {
        unsigned long long end = layout[i].addr + layout[i].size;

        if (addr >= layout[i].addr && (addr < end || end < layout[i].addr))
        {
            *region = layout[i];
            return 1;
        }
    }
    return 0;
}

static int broken_query( void *context, unsigned long long addr, struct horizon_region *region )
{
    (void)context;
    region->addr = addr + 0x1000;  /* does not contain addr */
    region->size = 0x1000;
    region->type = 0;
    return 1;
}

static int failing_query( void *context, unsigned long long addr, struct horizon_region *region )
{
    (void)context; (void)addr; (void)region;
    return 0;
}

int main(void)
{
    assert( !horizon_range_unmapped( 0x4ae0000, 0x90000, query, NULL ) );   /* the refused commit */
    assert( !horizon_range_unmapped( 0x4b00000, 0x10000, query, NULL ) );   /* inside the stack */
    assert( !horizon_range_unmapped( 0x4bf1000, 0x2000, query, NULL ) );    /* its last page */
    assert( horizon_range_unmapped( 0x4bf2000, 0x90000, query, NULL ) );    /* right after it */
    assert( horizon_range_unmapped( 0x49f2000, 0x100000, query, NULL ) );   /* between two stacks */
    assert( !horizon_range_unmapped( 0x49f2000, 0x100001, query, NULL ) );
    assert( horizon_range_unmapped( 0x1000000, 0x10000, query, NULL ) );
    assert( !horizon_range_unmapped( 0x7ffe0000, 0x1000, query, NULL ) );   /* in the heap region */
    queries = 0;
    assert( horizon_range_unmapped( 0xce400000, 0x100000000ull - 0xce400000 + 0x10000, query, NULL ) );
    assert( queries == 2 );                                                 /* two blocks, one to the top */
    assert( horizon_range_unmapped( 0xfffffffffff00000ull, 0xff000, query, NULL ) );   /* in the last block */
    assert( !horizon_range_unmapped( 0xfffffffffff00000ull, 0x200000, query, NULL ) );  /* the range wraps */
    assert( !horizon_range_unmapped( 0x1000000, 0x1000, broken_query, NULL ) );
    assert( !horizon_range_unmapped( 0x1000000, 0x1000, failing_query, NULL ) );
    puts( "Horizon free range: stacks, neighbours, heap, the top of the address space and bad queries passed" );
    return 0;
}
