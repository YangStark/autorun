/* A host stand-in for the Horizon kernel calls behind views of sections with no
 * file (horizon_memfile.h). mach_vm_remap shares pages between two addresses,
 * as svcMapProcessCodeMemory (an anchor) and svcMapProcessMemory (a view's
 * alias) do, and the stand-in checks what the kernel checks: an alias needs a
 * live anchor under its source, removing one names the pages it mapped, and
 * a heap page is code-mapped at most once. An anchored source is made
 * inaccessible, as Horizon locks it, so code that touches it there crashes
 * the test. It also refuses to remove an anchor that a view still aliases,
 * which Horizon would allow and leave that view impossible to unmap. */
#include <assert.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HOST_PAGE ((size_t)getpagesize())
#define HORIZON_MEMFILE_PAGE_SIZE HOST_PAGE
#define HORIZON_MEMFILE_ALLOC_PAGES(size) host_alloc_pages( size )
#define HORIZON_MEMFILE_FREE_PAGES(ptr, size) host_free_pages( ptr, size )

#define HOST_MAX_ANCHORS 64
#define HOST_MAX_ALIASED_PAGES 4096

struct host_anchor { void *addr; void *source; size_t size; int live; int placed; };
struct host_alias { void *dst; void *src; };  /* a page */

static struct host_anchor host_anchors[HOST_MAX_ANCHORS];
static struct host_alias host_aliases[HOST_MAX_ALIASED_PAGES];
static unsigned int host_alias_count;
static int host_pages_live, host_anchor_calls, host_unanchor_calls;
static int host_fail_anchor, host_fail_unanchor, host_fail_alias_countdown, host_fail_unalias;

static void *host_alloc_pages( size_t size )
{
    void *ptr = mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );

    if (ptr == MAP_FAILED) return NULL;
    host_pages_live++;
    return ptr;
}

static void host_free_pages( void *ptr, size_t size )
{
    assert( !munmap( ptr, size ) );
    host_pages_live--;
}

/* Address space with nothing mapped in it, for views and placed anchors. */
static void *host_reserve( size_t size )
{
    void *ptr = mmap( NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0 );

    assert( ptr != MAP_FAILED );
    return ptr;
}

static void host_placeholder( void *addr, size_t size )
{
    assert( mmap( addr, size, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0 ) == addr );
}

static void *host_remap( void *dst, void *src, size_t size )
{
    mach_vm_address_t target = (mach_vm_address_t)(uintptr_t)dst;
    vm_prot_t cur, max;
    kern_return_t kr;

    kr = mach_vm_remap( mach_task_self(), &target, size, 0,
                        dst ? VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE : VM_FLAGS_ANYWHERE,
                        mach_task_self(), (mach_vm_address_t)(uintptr_t)src, FALSE,
                        &cur, &max, VM_INHERIT_NONE );
    assert( kr == KERN_SUCCESS );
    return (void *)(uintptr_t)target;
}

static int host_ranges_overlap( const char *a, size_t a_size, const char *b, size_t b_size )
{
    return a < b + b_size && b < a + a_size;
}

static struct host_alias *host_find_alias( void *dst )
{
    unsigned int i;

    for (i = 0; i < host_alias_count; i++) if (host_aliases[i].dst == dst) return &host_aliases[i];
    return NULL;
}

/* svcMapProcessCodeMemory of source at addr, or where the kernel likes if NULL. */
static void *host_anchor( void *addr, void *source, size_t size )
{
    unsigned int i, slot = HOST_MAX_ANCHORS;

    assert( size && !(size % HOST_PAGE) && !((uintptr_t)source % HOST_PAGE) );
    for (i = 0; i < HOST_MAX_ANCHORS; i++)
    {
        if (!host_anchors[i].live) { if (slot == HOST_MAX_ANCHORS) slot = i; continue; }
        assert( !host_ranges_overlap( host_anchors[i].source, host_anchors[i].size, source, size ) );
        if (addr) assert( !host_ranges_overlap( host_anchors[i].addr, host_anchors[i].size, addr, size ) );
    }
    assert( slot < HOST_MAX_ANCHORS );
    if (host_fail_anchor)
    {
        errno = ENOMEM;
        return NULL;
    }
    host_anchors[slot].placed = addr != NULL;
    host_anchors[slot].addr = host_remap( addr, source, size );
    host_anchors[slot].source = source;
    host_anchors[slot].size = size;
    host_anchors[slot].live = 1;
    assert( !mprotect( source, size, PROT_NONE ) );
    host_anchor_calls++;
    return host_anchors[slot].addr;
}

/* svcUnmapProcessCodeMemory. */
static int host_unanchor( void *addr, void *source, size_t size )
{
    unsigned int i;

    for (i = 0; i < HOST_MAX_ANCHORS; i++)
        if (host_anchors[i].live && host_anchors[i].addr == addr) break;
    assert( i < HOST_MAX_ANCHORS && host_anchors[i].source == source && host_anchors[i].size == size );
    for (unsigned int j = 0; j < host_alias_count; j++)
        assert( !host_ranges_overlap( host_aliases[j].src, HOST_PAGE, addr, size ) );
    if (host_fail_unanchor)
    {
        errno = EBUSY;
        return -1;
    }
    assert( !mprotect( source, size, PROT_READ | PROT_WRITE ) );
    if (host_anchors[i].placed) host_placeholder( addr, size );
    else assert( !munmap( addr, size ) );
    host_anchors[i].live = 0;
    host_unanchor_calls++;
    return 0;
}

/* svcMapProcessMemory from our own process. */
static int host_alias( void *dst, void *src, size_t size )
{
    unsigned int i, anchored = 0;
    size_t page;

    assert( size && !(size % HOST_PAGE) && !((uintptr_t)dst % HOST_PAGE) && !((uintptr_t)src % HOST_PAGE) );
    for (i = 0; i < HOST_MAX_ANCHORS; i++)
        if (host_anchors[i].live && (char *)src >= (char *)host_anchors[i].addr &&
            (char *)src + size <= (char *)host_anchors[i].addr + host_anchors[i].size)
            anchored = 1;
    assert( anchored );
    for (page = 0; page < size; page += HOST_PAGE) assert( !host_find_alias( (char *)dst + page ) );
    if (host_fail_alias_countdown && !--host_fail_alias_countdown)
    {
        errno = ENOMEM;
        return -1;
    }
    host_remap( dst, src, size );
    for (page = 0; page < size; page += HOST_PAGE)
    {
        assert( host_alias_count < HOST_MAX_ALIASED_PAGES );
        host_aliases[host_alias_count].dst = (char *)dst + page;
        host_aliases[host_alias_count].src = (char *)src + page;
        host_alias_count++;
    }
    return 0;
}

/* svcUnmapProcessMemory: the kernel compares the pages at dst and src. */
static int host_unalias( void *dst, void *src, size_t size )
{
    size_t page;

    if (host_fail_unalias)
    {
        errno = EINVAL;
        return -1;
    }
    for (page = 0; page < size; page += HOST_PAGE)
    {
        struct host_alias *alias = host_find_alias( (char *)dst + page );

        assert( alias && alias->src == (char *)src + page );
        *alias = host_aliases[--host_alias_count];
    }
    host_placeholder( dst, size );
    return 0;
}

static int host_live_anchors(void)
{
    int i, count = 0;

    for (i = 0; i < HOST_MAX_ANCHORS; i++) count += host_anchors[i].live;
    return count;
}
