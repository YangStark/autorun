/*
 * Whether a range of the address space is unmapped, as the kernel sees it.
 *
 * Wine's free-area search knows Wine's own views only. libnx maps thread stacks
 * (virtmemFindStack) and JIT code (virtmemFindCodeMemory) wherever its own
 * bookkeeping has room, and under a 32-bit address space those share the low
 * gigabyte with Wine's views: a reservation laid over a thread stack fails at
 * its first commit with InvalidCurrentMemory. horizon.c asks svcQueryMemory
 * through this walk before reserving.
 */

#ifndef __WINE_HORIZON_FREE_RANGE_H
#define __WINE_HORIZON_FREE_RANGE_H

#define HORIZON_MEMTYPE_UNMAPPED 0  /* MemType_Unmapped */

struct horizon_region
{
    unsigned long long addr;
    unsigned long long size;
    unsigned int type;
};

/* Fills region with the block containing addr; returns 0 on failure. */
typedef int (*horizon_region_query)( void *context, unsigned long long addr, struct horizon_region *region );

/* 1 when every byte of [start, start + size) lies in unmapped blocks. */
static inline int horizon_range_unmapped( unsigned long long start, unsigned long long size,
                                          horizon_region_query query, void *context )
{
    unsigned long long addr = start, end = start + size;
    struct horizon_region region;

    if (end < start) return 0;
    while (addr < end)
    {
        unsigned long long next;

        if (!query( context, addr, &region ) || region.type != HORIZON_MEMTYPE_UNMAPPED) return 0;
        if (region.addr > addr) return 0;  /* a block that does not contain addr */
        next = region.addr + region.size;
        /* The last block reaches the top of the address space and its end wraps. */
        if (next < region.addr) return 1;
        if (next <= addr) return 0;  /* a block that does not contain addr */
        addr = next;
    }
    return 1;
}

#endif /* __WINE_HORIZON_FREE_RANGE_H */
