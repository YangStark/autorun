/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * Host support for Box64's ARM64 dynamic recompiler in the WoW64 engine
 * (wow64_box64_engine.c): code arenas, jump tables and the memory queries
 * the dynarec makes. Adapted from WineBox64 NX's Horizon glue
 * (source/box64_nx_dynarec.c, MIT License, Copyright 2026 WineBox64 NX
 * contributors).
 *
 * Horizon never maps memory writable and executable at once. Each arena has a
 * writable alias the dynarec emits into and an executable alias it runs from
 * (cmake/Box64Core.cmake moves block pointers to the latter). Linux test hosts
 * build the same split with a shared memory file, so the path is exercised
 * before hardware.
 *
 * Guest code is not write-protected after translation (protectDB is a no-op).
 * Blocks link directly. winebox64 forwards WoW64's reports of freed, unmapped,
 * re-protected and flushed guest memory to wine_nx_box64_invalidate, which
 * frees the blocks there or makes their next entry check the code's hash.
 * Code a program changes without such a report (self-modifying code without
 * NtFlushInstructionCache) keeps running its old translation.
 * Runs stop at the gate pages because they are reported non-executable: the
 * dynarec leaves them to the interpreter, whose instruction hook stops there.
 */
#ifndef __SWITCH__
#define _GNU_SOURCE /* memfd_create */
#endif
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "box64_options.h"
#include "alternate.h"
#include "box64context.h"
#include "bridge.h"
#include "custommem.h"
#include "debug.h"
#include "dynablock.h"
#include "dynarec/dynablock_private.h"
#include "dynarec/dynarec_next.h"
#include "dynarec/native_lock.h"
#include "elfloader.h"
#include "emu/x64emu_private.h"
#include "env.h"
#include "rbtree.h"
#include "x64emu.h"
#include "x64test.h"
#include "x64trace.h"

/* Translated code lives in code memory from jitCreate, one kernel code memory
 * object per arena. Atmosphère creates only 10 of those objects for the whole
 * system (kern_init_slab_setup.cpp, SlabCountKCodeMemory), so 8 MB arenas
 * stopped translation at 80 MB, and WarCraft III went on in the interpreter
 * too slowly to draw a frame. Arenas double from 16 MB to 256 MB and take
 * memory only once needed. */
#define NX_ARENA_FIRST    (16 * 1024 * 1024)
#define NX_ARENA_LARGEST  (256 * 1024 * 1024)
#define NX_ARENA_SMALLEST (1024 * 1024)
#define NX_MAX_ARENAS 32
#define NX_LOCK_ADDRESS_SLOTS 8192
#define NX_MAX_STOP_PAGES 4

struct nx_arena
{
#ifdef __SWITCH__
    Jit jit;
#endif
    uint8_t *rw;
    uint8_t *rx;
    size_t size;
    size_t used;
    uint8_t *starts;  /* a bit per 16 bytes, set where an allocation begins */
};

static struct nx_arena arenas[NX_MAX_ARENAS];
static unsigned int arena_count;
static pthread_mutex_t arena_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static int dynarec_ready;

/* Pages the dynarec must not translate: the syscall/Unix-call gates, and a
 * test completion address. Written once per process, read lock-free. */
static uintptr_t stop_pages[NX_MAX_STOP_PAGES];
static unsigned int stop_page_count;
static pthread_mutex_t stop_page_mutex = PTHREAD_MUTEX_INITIALIZER;

uintptr_t box64_pagesize = 4096;
/* Use the same nanosecond timebase as the interpreter. Direct CNTVCT_EL0
 * reads emitted by Box64 are not available to this Horizon process. */
int box64_rdtsc = 1;
char *ftrace_name = NULL; /* no Box64 trace file; log levels default from it */
cpu_ext_t cpuext = {0}; /* no optional host instructions: portable across Switch models */

uint64_t wine_nx_box64_dynarec_bytes;
uint64_t wine_nx_box64_arena_bytes;  /* code memory reserved in all arenas */
extern void wine_nx_runtime_trace( const char *msg ) __attribute__((weak));
unsigned long long wine_nx_box64_native_entries;
unsigned int wine_nx_box64_block_tests;  /* hash validations in DBGetBlock */
static size_t max_block_size;            /* the largest guest block translated */

#if JMPTABL_SHIFTMAX != 16
#error Jump-table top-level shift must be 16
#endif
#ifdef JMPTABL_SHIFT4
static uintptr_t ****jmptbl4[1 << JMPTABL_SHIFT4];
static uintptr_t ***jmptbl_default3[1 << JMPTABL_SHIFT3];
static uintptr_t ***jmptbl_48[1 << JMPTABL_SHIFT3];
#else
static uintptr_t ***jmptbl3[1 << JMPTABL_SHIFT3];
static uintptr_t **jmptbl_48[1 << JMPTABL_SHIFT2];
#endif
static uintptr_t **jmptbl_default2[1 << JMPTABL_SHIFT2];
static uintptr_t *jmptbl_default1[1 << JMPTABL_SHIFT1];
static uintptr_t jmptbl_default0[1 << JMPTABL_SHIFT0];
static uintptr_t jmptbl_oom_entry;

static uintptr_t lock_addresses[NX_LOCK_ADDRESS_SLOTS];
static int lock_addresses_saturated;

static size_t align_up( size_t value, size_t alignment )
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/* Called with arena_mutex held. Maps an arena of size bytes, or returns 0 with
 * the reason in *rc. */
static int map_arena( struct nx_arena *arena, size_t size, unsigned int *rc )
{
    memset( arena, 0, sizeof(*arena) );
    *rc = 0;
#ifdef __SWITCH__
    {
        Result res = jitCreate( &arena->jit, size );

        if (R_FAILED( res ))
        {
            *rc = res;
            return 0;
        }
    }
    if (arena->jit.type != JitType_CodeMemory)
    {
        jitClose( &arena->jit );
        return 0;
    }
    arena->rw = jitGetRwAddr( &arena->jit );
    arena->rx = jitGetRxAddr( &arena->jit );
    if (!arena->rw || !arena->rx)
    {
        jitClose( &arena->jit );
        return 0;
    }
#else
    {
        int fd = memfd_create( "wine-nx-dynarec", 0 );
        void *rw = MAP_FAILED, *rx = MAP_FAILED;

        if (fd == -1) return 0;
        if (!ftruncate( fd, size ))
        {
            rw = mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
            rx = mmap( NULL, size, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0 );
        }
        close( fd );
        if (rw == MAP_FAILED || rx == MAP_FAILED)
        {
            if (rw != MAP_FAILED) munmap( rw, size );
            if (rx != MAP_FAILED) munmap( rx, size );
            return 0;
        }
        arena->rw = rw;
        arena->rx = rx;
    }
#endif
    arena->size = size;
    return 1;
}

/* Gives the arenas' code memory back on the way to the launcher. An arena is a
 * kernel code memory object over heap pages, and while it lives those pages are
 * lent to it: the loader cannot reset a heap that holds them. Nothing may run
 * translated code afterwards, so this is only for a process on its way out, and
 * it is called once the program's threads are gone. Returns how many it closed
 * and, through bytes, how much heap they held. */
unsigned int wine_nx_box64_release_arenas( unsigned long long *bytes )
{
    unsigned int i, closed = 0;
    unsigned long long held = 0;

    pthread_mutex_lock( &arena_mutex );
    for (i = 0; i < arena_count; i++)
    {
        struct nx_arena *arena = &arenas[i];

        if (!arena->size) continue;
#ifdef __SWITCH__
        jitClose( &arena->jit );
#else
        munmap( arena->rw, arena->size );
        munmap( arena->rx, arena->size );
#endif
        held += arena->size;
        free( arena->starts );
        arena->starts = NULL;
        arena->rw = arena->rx = NULL;
        arena->size = arena->used = 0;
        closed++;
    }
    arena_count = 0;
    dynarec_ready = 0;
    wine_nx_box64_arena_bytes = 0;
    pthread_mutex_unlock( &arena_mutex );
    if (bytes) *bytes = held;
    return closed;
}

/* Called with arena_mutex held. The next arena doubles the last one, from
 * NX_ARENA_FIRST up to NX_ARENA_LARGEST, and holds at least minimum bytes;
 * smaller sizes are tried when the heap has no block that large. When even
 * NX_ARENA_SMALLEST fails the kernel has no code memory left, and later calls
 * give up at once instead of asking again for every block. */
static int create_arena( size_t minimum )
{
    static int exhausted;
    struct nx_arena *arena;
    size_t smallest = align_up( NX_ARENA_SMALLEST, 0x1000 );
    size_t floor = align_up( minimum > smallest ? minimum : smallest, 0x1000 );
    size_t size = NX_ARENA_FIRST;
    unsigned int i, rc = 0;
    char message[192];

    if (exhausted || arena_count >= NX_MAX_ARENAS) return 0;
    for (i = 0; i < arena_count && size < NX_ARENA_LARGEST; i++) size *= 2;
    if (size < floor) size = floor;
    arena = &arenas[arena_count];
    while (!map_arena( arena, size, &rc ))
    {
        if (size <= floor)
        {
            if (floor == smallest) exhausted = 1;
            snprintf( message, sizeof(message), "[DYNAREC] no code arena after %u (%llu MB): rc=%#x for %zu KB; "
                      "new x86 code runs in the interpreter", arena_count,
                      (unsigned long long)(wine_nx_box64_arena_bytes >> 20), rc, minimum >> 10 );
            if (&wine_nx_runtime_trace) wine_nx_runtime_trace( message );
            return 0;
        }
        size = align_up( size / 2 > floor ? size / 2 : floor, 0x1000 );
    }
    arena->starts = calloc( (size / 16 + 7) / 8, 1 );  /* without it, faults are just not described */
    wine_nx_box64_arena_bytes += size;
    snprintf( message, sizeof(message), "[DYNAREC] code arena %u: %zu MB (%llu MB in all)", arena_count + 1,
              size >> 20, (unsigned long long)(wine_nx_box64_arena_bytes >> 20) );
    if (&wine_nx_runtime_trace) wine_nx_runtime_trace( message );
    __atomic_store_n( &arena_count, arena_count + 1, __ATOMIC_RELEASE );
    return 1;
}

static struct nx_arena *find_arena( const void *address, size_t *offset )
{
    uintptr_t target = (uintptr_t)address;
    unsigned int i, count = __atomic_load_n( &arena_count, __ATOMIC_ACQUIRE );

    for (i = 0; i < count; i++)
    {
        struct nx_arena *arena = &arenas[i];
        if (target - (uintptr_t)arena->rw < arena->size)
        {
            *offset = target - (uintptr_t)arena->rw;
            return arena;
        }
        if (target - (uintptr_t)arena->rx < arena->size)
        {
            *offset = target - (uintptr_t)arena->rx;
            return arena;
        }
    }
    return NULL;
}

static void init_jump_tables(void)
{
    size_t i;
#ifdef JMPTABL_SHIFT4
    for (i = 0; i < (1u << JMPTABL_SHIFT4); i++) jmptbl4[i] = jmptbl_default3;
    for (i = 0; i < (1u << JMPTABL_SHIFT3); i++) jmptbl_default3[i] = jmptbl_48[i] = jmptbl_default2;
    jmptbl4[0] = jmptbl_48;
#else
    for (i = 0; i < (1u << JMPTABL_SHIFT3); i++) jmptbl3[i] = jmptbl_default2;
    for (i = 0; i < (1u << JMPTABL_SHIFT2); i++) jmptbl_48[i] = jmptbl_default1;
    jmptbl3[0] = jmptbl_48;
#endif
    for (i = 0; i < (1u << JMPTABL_SHIFT2); i++) jmptbl_default2[i] = jmptbl_default1;
    for (i = 0; i < (1u << JMPTABL_SHIFT1); i++) jmptbl_default1[i] = jmptbl_default0;
    for (i = 0; i < (1u << JMPTABL_SHIFT0); i++) jmptbl_default0[i] = (uintptr_t)native_next;
    jmptbl_oom_entry = (uintptr_t)native_next;
}

/* The options a program's options file may set (box64_options.h): trade-offs in
 * the code the dynarec generates. WAIT stays on, as below. */
static const struct { const char *name; int *value; int min, max; } nx_box64_options[] =
{
    { "BOX64_DYNAREC_ALIGNED_ATOMICS", &box64env.dynarec_aligned_atomics, 0, 1 },
    { "BOX64_DYNAREC_BIGBLOCK", &box64env.dynarec_bigblock, 0, 3 },
    { "BOX64_DYNAREC_CALLRET", &box64env.dynarec_callret, 0, 2 },
    { "BOX64_DYNAREC_DF", &box64env.dynarec_df, 0, 1 },
    { "BOX64_DYNAREC_DIV0", &box64env.dynarec_div0, 0, 1 },
    { "BOX64_DYNAREC_FASTNAN", &box64env.dynarec_fastnan, 0, 1 },
    { "BOX64_DYNAREC_FASTROUND", &box64env.dynarec_fastround, 0, 2 },
    { "BOX64_DYNAREC_FORWARD", &box64env.dynarec_forward, 0, 1024 },
    { "BOX64_DYNAREC_NATIVEFLAGS", &box64env.dynarec_nativeflags, 0, 1 },
    { "BOX64_DYNAREC_PAUSE", &box64env.dynarec_pause, 0, 3 },
    { "BOX64_DYNAREC_SAFEFLAGS", &box64env.dynarec_safeflags, 0, 2 },
    { "BOX64_DYNAREC_STRONGMEM", &box64env.dynarec_strongmem, 0, 3 },
    { "BOX64_DYNAREC_WEAKBARRIER", &box64env.dynarec_weakbarrier, 0, 2 },
    { "BOX64_DYNAREC_X87DOUBLE", &box64env.dynarec_x87double, 0, 2 },
};

/* Set by the runtime before the first run: the program's .box64.txt. */
char wine_nx_box64_options_path[512];
extern void wine_nx_runtime_trace( const char *msg ) __attribute__((weak));

static void apply_box64_options(void)
{
    char line[256], name[64], message[768];
    const char *file_name = strrchr( wine_nx_box64_options_path, '/' );
    size_t used, i;
    long value;
    FILE *file;

    if (!wine_nx_box64_options_path[0] || !(file = fopen( wine_nx_box64_options_path, "r" ))) return;
    used = snprintf( message, sizeof(message), "[BOX64] %s:", file_name ? file_name + 1 : wine_nx_box64_options_path );
    while (fgets( line, sizeof(line), file ))
    {
        if (!nx_box64_option_line( line, name, sizeof(name), &value )) continue;
        for (i = 0; i < sizeof(nx_box64_options) / sizeof(nx_box64_options[0]); i++)
            if (!strcmp( name, nx_box64_options[i].name )) break;
        if (i == sizeof(nx_box64_options) / sizeof(nx_box64_options[0]) ||
            value < nx_box64_options[i].min || value > nx_box64_options[i].max)
        {
            if (used < sizeof(message)) used += snprintf( message + used, sizeof(message) - used, " %s=%ld refused", name, value );
            continue;
        }
        *nx_box64_options[i].value = (int)value;
        if (used < sizeof(message)) used += snprintf( message + used, sizeof(message) - used, " %s=%ld", name, value );
    }
    fclose( file );
    if (&wine_nx_runtime_trace) wine_nx_runtime_trace( message );
}

static void init_box64_env(void)
{
    /* Box64's own defaults for every option, then the CPU this backend
     * presents: nothing beyond SSE2 (dlls/winebox64/cpuid.h). */
#define INTEGER(NAME, name, default, min, max, wine) box64env.name = default;
#define INTEGER64(NAME, name, default, wine) box64env.name = default;
#define BOOLEAN(NAME, name, default, wine) box64env.name = default;
#define ADDRESS(NAME, name, wine)
#define STRING(NAME, name, wine)
    ENVSUPER()
#undef INTEGER
#undef INTEGER64
#undef BOOLEAN
#undef ADDRESS
#undef STRING
    box64env.dynarec = 1;
    box64env.log = LOG_NONE;
    box64env.dynarec_log = LOG_NONE;
    box64env.avx = 0;
    box64env.aes = 0;
    box64env.pclmulqdq = 0;
    box64env.shaext = 0;
    box64env.sse42 = 0;
    /* CALLRET: translated RETs return natively to the code after their CALL.
     * Level 2 puts a guard instruction at each such return site, which Box64
     * turns into ARCH_UDF when the block may have changed and back once checked;
     * those writes go through the writable alias (Box64Core.cmake), and the
     * trap they cause is wine_nx_box64_callret_trap. Level 1 has no guard and
     * would return into a stale translation. */
    box64env.dynarec_callret = 2;
    box64env.dynarec_wait = 1; /* tracked lock ownership for translation faults */
    /* A division by zero raises EXCEPTION_INT_DIVIDE_BY_ZERO, which programs
     * catch, instead of the 0 ARM64's UDIV gives: one compare next to a slow
     * division. The interpreter always checks. */
    box64env.dynarec_div0 = 1;
    apply_box64_options();
}

static void init_dynarec(void)
{
    pthread_mutex_init( &my_context->mutex_dyndump, NULL );
    pthread_mutex_init( &my_context->mutex_trace, NULL );
    pthread_mutex_init( &my_context->mutex_tls, NULL );
    pthread_mutex_init( &my_context->mutex_thread, NULL );
    pthread_mutex_init( &my_context->mutex_bridge, NULL );
    init_box64_env();
    init_jump_tables();
    pthread_mutex_lock( &arena_mutex );
    dynarec_ready = create_arena( 0 );
    pthread_mutex_unlock( &arena_mutex );
    if (!dynarec_ready) box64env.dynarec = 0;
}

/* Once per process, before the first run. Returns FALSE when no code memory
 * could be created; the engine then interprets. */
int wine_nx_box64_dynarec_init(void)
{
    /* Every run asks; once set, dynarec_ready stays. */
    if (dynarec_ready) return 1;
    pthread_once( &init_once, init_dynarec );
    return dynarec_ready;
}

void wine_nx_box64_dynarec_add_stop( uint32_t address )
{
    uintptr_t page = address & ~0xfffu;
    unsigned int i, count = __atomic_load_n( &stop_page_count, __ATOMIC_ACQUIRE );

    if (!address) return;
    /* Every run registers the same gates, so they are nearly always listed
     * already; pages are only ever added, each before the count covers it. */
    for (i = 0; i < count; i++) if (stop_pages[i] == page) return;
    pthread_mutex_lock( &stop_page_mutex );
    for (i = 0; i < stop_page_count; i++) if (stop_pages[i] == page) break;
    if (i == stop_page_count && stop_page_count < NX_MAX_STOP_PAGES)
    {
        stop_pages[stop_page_count] = page;
        __atomic_store_n( &stop_page_count, stop_page_count + 1, __ATOMIC_RELEASE );
    }
    pthread_mutex_unlock( &stop_page_mutex );
}

dynablock_t *CreateEmptyBlock( uintptr_t addr, int is32bits, int is_new );

/* A gate is a stop, and also gets an empty block: translated code jumps to it
 * through the jump table, whose default entry would take it through
 * native_next, LinkNext and a failed block lookup first. An empty block's code
 * goes straight to the epilog, with the gate in x27 as the jump left it. */
void wine_nx_box64_dynarec_add_gate( uint32_t address )
{
    dynablock_t *block;

    wine_nx_box64_dynarec_add_stop( address );
    if (!address || !dynarec_ready || !isJumpTableDefault64( (void *)(uintptr_t)address )) return;
    mutex_lock( &my_context->mutex_dyndump );
    if (isJumpTableDefault64( (void *)(uintptr_t)address ) && (block = CreateEmptyBlock( address, 1, 1 )) &&
        !addJumpTableIfDefault64( (void *)(uintptr_t)address, block->block ))
        FreeDynablock( block, 0, 0 );
    mutex_unlock( &my_context->mutex_dyndump );
}

/* Called by FillBlock64, which runs under the translator lock. */
void wine_nx_box64_note_block_size( size_t size )
{
    if (size > max_block_size) max_block_size = size;
}

/* Guest memory at [addr, addr + size) was freed or unmapped (destroy), or
 * re-protected or flushed: free the translated blocks overlapping it, or make
 * their next entry check the code's hash, as Box64's cleanDBFromAddressRange
 * does. Each block has one jump table entry, at its first guest address, and a
 * block starting up to the largest block size earlier may reach into the
 * range. Unused table levels are skipped whole. */
static inline uintptr_t next_table_boundary( uintptr_t pos, unsigned int shift )
{
    return (pos | (((uintptr_t)1 << shift) - 1)) + 1;
}

/* For [PROGRESS]: reports of changed guest memory, and lookups that found their
 * block marked by one, so that entering it checks its code under the
 * translator's lock (Box64's DBGetBlock). */
unsigned int wine_nx_box64_invalidations, wine_nx_box64_marked_lookups;

void wine_nx_box64_invalidate( uintptr_t addr, size_t size, int destroy )
{
    uintptr_t end, pos;

    if (!size || !dynarec_ready || addr > 0xffffffffu) return;
    __atomic_add_fetch( &wine_nx_box64_invalidations, 1, __ATOMIC_RELAXED );
    end = size > 0x100000000ull - addr ? 0x100000000ull : addr + size;
    size = end - addr;
    for (pos = addr > max_block_size ? addr - max_block_size : 0; pos < end;)
    {
        uintptr_t *entries, target;
        dynablock_t *db;
#ifdef JMPTABL_SHIFT4
        uintptr_t ****level3 = jmptbl4[(pos >> JMPTABL_START4) & JMPTABLE_MASK4];
        uintptr_t ***level2;
        uintptr_t **level1;

        if (level3 == jmptbl_default3)
        {
            pos = next_table_boundary( pos, JMPTABL_START4 );
            continue;
        }
        level2 = level3[(pos >> JMPTABL_START3) & JMPTABLE_MASK3];
#else
        uintptr_t ***level2 = jmptbl3[(pos >> JMPTABL_START3) & JMPTABLE_MASK3];
        uintptr_t **level1;
#endif
        if (level2 == jmptbl_default2)
        {
            pos = next_table_boundary( pos, JMPTABL_START3 );
            continue;
        }
        level1 = level2[(pos >> JMPTABL_START2) & JMPTABLE_MASK2];
        if (level1 == jmptbl_default1)
        {
            pos = next_table_boundary( pos, JMPTABL_START2 );
            continue;
        }
        entries = level1[(pos >> JMPTABL_START1) & JMPTABLE_MASK1];
        if (entries == jmptbl_default0)
        {
            pos = next_table_boundary( pos, JMPTABL_START1 );
            continue;
        }
        target = entries[pos & JMPTABLE_MASK0];
        if (target != (uintptr_t)native_next && (db = *(dynablock_t **)(target - sizeof(void *))))
        {
            if (destroy) FreeRangeDynablock( db, addr, size );
            else MarkRangeDynablock( db, addr, size );
        }
        pos++;
    }
}

uintptr_t AllocDynarecMap( uintptr_t x64_addr, size_t size, int is_new )
{
    struct nx_arena *arena = NULL;
    uintptr_t result;
    unsigned int i;

    (void)x64_addr;
    (void)is_new;
    if (!size || !dynarec_ready) return 0;
    size = align_up( size, 16 );
    pthread_mutex_lock( &arena_mutex );
    for (i = 0; i < arena_count && !arena; i++)
        if (size <= arenas[i].size - arenas[i].used) arena = &arenas[i];
    if (!arena && create_arena( size )) arena = &arenas[arena_count - 1];
    if (!arena)
    {
        pthread_mutex_unlock( &arena_mutex );
        return 0;
    }
    result = (uintptr_t)arena->rw + arena->used;
    if (arena->starts) arena->starts[arena->used / 16 >> 3] |= 1 << (arena->used / 16 & 7);
    arena->used += size;
    __atomic_add_fetch( &wine_nx_box64_dynarec_bytes, size, __ATOMIC_RELAXED );
    pthread_mutex_unlock( &arena_mutex );
    return result;
}

void FreeDynarecMap( uintptr_t addr )
{
    (void)addr; /* bump allocation; blocks are never freed yet */
}

void *DynarecMapExecutableAddress( void *addr )
{
    size_t offset;
    struct nx_arena *arena = find_arena( addr, &offset );
    return arena ? arena->rx + offset : addr;
}

void *DynarecMapWritableAddress( void *addr )
{
    size_t offset;
    struct nx_arena *arena = find_arena( addr, &offset );
    return arena ? arena->rw + offset : addr;
}

void DynarecMapClearCache( void *addr, size_t size )
{
    size_t offset;
    struct nx_arena *arena = find_arena( addr, &offset );

    if (!arena) return;
    if (size > arena->size - offset) size = arena->size - offset;
#ifdef __SWITCH__
    armDCacheFlush( arena->rw + offset, size );
    armICacheInvalidate( arena->rx + offset, size );
#else
    __builtin___clear_cache( (char *)arena->rx + offset, (char *)arena->rx + offset + size );
#endif
}

/* For the exception handler: whether pc lies in translated code, where a
 * resumed fault must keep x16 and x17 (the guest's ESI and EDI). Lock-free:
 * arenas are never freed. */
int wine_nx_box64_is_translated_pc( uintptr_t pc )
{
    size_t offset;
    struct nx_arena *arena = find_arena( (void *)pc, &offset );

    return arena && offset < arena->used;
}

/* For the exception handler: names the x86 instruction behind a native pc in
 * translated code, with the guest registers a block keeps in x10-x17. It runs
 * on the libnx exception stack without locks, which is safe because
 * allocations are never freed: a start bit, once set, stays, and a block's
 * dynablock_t lies inside its own allocation. Returns 0 outside the code. */
/* The allocation starts with a pointer to its dynablock_t, written through the
 * writable alias; Box64Core.cmake moves the block's own code pointers
 * (actual_block, block, jmpnext) to the executable alias once it is emitted. */
static dynablock_t *block_at( const struct nx_arena *arena, size_t offset )
{
    size_t bit = offset / 16, first = bit > (1u << 20) / 16 ? bit - (1u << 20) / 16 : 0;  /* no block spans a megabyte */
    uintptr_t start, actual;
    dynablock_t *db;

    if (!arena->starts || offset >= arena->used) return NULL;
    while (!(arena->starts[bit >> 3] & (1 << (bit & 7))))
    {
        if (bit == first) return NULL;
        bit--;
    }
    start = (uintptr_t)arena->rw + bit * 16;
    db = *(dynablock_t **)start;
    if ((uintptr_t)db - start >= arena->used - bit * 16) return NULL;
    actual = (uintptr_t)db->actual_block;
    if (actual != (uintptr_t)arena->rx + bit * 16 && actual != start) return NULL;
    return db;
}

int wine_nx_box64_describe_native_pc( uintptr_t pc, const unsigned long long *x, char *buf, size_t size )
{
    size_t offset;
    struct nx_arena *arena = find_arena( (void *)pc, &offset );
    uintptr_t x64 = 0;
    dynablock_t *db;

    if (!arena) return 0;
    if (!arena->starts || offset >= arena->used)
    {
        snprintf( buf, size, "[BOX64 FAULT] pc=%lx in the code arena but %s", (unsigned long)pc,
                  arena->starts ? "past its allocations" : "without a block map (out of memory?)" );
        return 1;
    }
    if (!(db = block_at( arena, offset )))
    {
        snprintf( buf, size, "[BOX64 FAULT] pc=%lx in translated code, no block found for it eax=%08x ecx=%08x "
                  "edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x", (unsigned long)pc,
                  (unsigned)x[10], (unsigned)x[11], (unsigned)x[12], (unsigned)x[13],
                  (unsigned)x[14], (unsigned)x[15], (unsigned)x[16], (unsigned)x[17] );
        return 1;
    }
    if (db->done) x64 = getX64Address( db, (uintptr_t)arena->rx + offset );
    snprintf( buf, size, "[BOX64 FAULT] x86=%08lx block=%08lx+%lx%s eax=%08x ecx=%08x edx=%08x ebx=%08x "
              "esp=%08x ebp=%08x esi=%08x edi=%08x",
              (unsigned long)x64, (unsigned long)(uintptr_t)db->x64_addr, (unsigned long)db->x64_size,
              db->done ? "" : " (unfinished)", (unsigned)x[10], (unsigned)x[11], (unsigned)x[12],
              (unsigned)x[13], (unsigned)x[14], (unsigned)x[15], (unsigned)x[16], (unsigned)x[17] );
    return 1;
}

/* For the sampling profiler (thread_profile.c), from another thread than the
 * one running pc, lock-free for the same reason: the x86 address behind pc, or
 * its block's start while the block is being written. 0 outside translated code. */
int wine_nx_box64_pc_to_x86( uintptr_t pc, uintptr_t *x86 )
{
    size_t offset;
    struct nx_arena *arena = find_arena( (void *)pc, &offset );
    dynablock_t *db;

    if (!arena || !(db = block_at( arena, offset ))) return 0;
    /* The block's code pointers are executable-alias addresses, and so is pc. */
    *x86 = db->done ? getX64Address( db, (uintptr_t)arena->rx + offset ) : (uintptr_t)db->x64_addr;
    return 1;
}

/* For [PROGRESS]: native returns that hit a marked return site, by outcome. */
unsigned int wine_nx_box64_callret_clean, wine_nx_box64_callret_dirty;
extern void arm64_epilog(void);
/* A return site's two states, as dynarec/dynarec_arch.h defines them for ARM64
 * (that header needs the code generator's own headers). */
#define ARCH_NOP 0b11010101000000110010000000011111
#define ARCH_UDF 0xcafe

static int guest_code_readable( void *addr, uintptr_t size )
{
#ifdef __SWITCH__
    u64 pos = (uintptr_t)addr, end = pos + size;

    while (pos < end)
    {
        MemoryInfo info;
        u32 page;

        if (R_FAILED( svcQueryMemory( &info, &page, pos ) ) || !(info.perm & Perm_R) ||
            info.type == MemType_Unmapped)
            return 0;
        pos = info.addr + info.size;
    }
#else
    (void)addr;
    (void)size;
#endif
    return 1;
}

/* A native return (CALLRET) landed on a return site its block marked ARCH_UDF
 * because the block's code may have changed. As Box64's SIGILL handler does: if
 * the code is unchanged, put the guards back to ARCH_NOP and go on after the
 * site; otherwise leave the block through the epilog, which stores the guest
 * registers (x0 holds the emulator, x27 the x86 return address, x28 the frame)
 * and returns to EmuRun, which translates that address again. Called from the
 * exception handler, so nothing here locks; a block, its site list and its
 * code are never freed. Returns 0 when pc is not a marked return site. */
int wine_nx_box64_callret_trap( uintptr_t *pc )
{
    size_t offset;
    struct nx_arena *arena = find_arena( (void *)*pc, &offset );
    dynablock_t *db;
    int i, site = 0;

    if (!arena || offset + 4 > arena->used || *(const uint32_t *)(arena->rw + offset) != ARCH_UDF) return 0;
    if (!(db = block_at( arena, offset )) || !db->callret_size) return 0;
    for (i = 0; i < db->callret_size && !site; i++)
        site = (uintptr_t)db->block + db->callrets[i].offs == *pc && !db->callrets[i].type;
    if (!site) return 0;

    if (!db->gone && guest_code_readable( db->x64_addr, db->x64_size ) &&
        X31_hash_code( db->x64_addr, (int)db->x64_size ) == db->hash)
    {
        if (db->always_test) protectDB( (uintptr_t)db->x64_addr, 1 );
        else
        {
            for (i = 0; i < db->callret_size; i++)
                *(uint32_t *)DynarecMapWritableAddress( (char *)db->block + db->callrets[i].offs ) = ARCH_NOP;
            DynarecMapClearCache( db->block, db->size );
            protectDBJumpTable( (uintptr_t)db->x64_addr, db->x64_size, db->block, db->jmpnext );
        }
        *pc += 4;
        __atomic_add_fetch( &wine_nx_box64_callret_clean, 1, __ATOMIC_RELAXED );
        return 1;
    }
    dynablock_leave_runtime( db );
    *pc = (uintptr_t)arm64_epilog;
    __atomic_add_fetch( &wine_nx_box64_callret_dirty, 1, __ATOMIC_RELAXED );
    return 1;
}

#ifdef JMPTABL_SHIFT4
static uintptr_t *create_jump_table( uintptr_t idx0, uintptr_t idx1, uintptr_t idx2, uintptr_t idx3,
                                     uintptr_t idx4, int for32bits )
{
    size_t i;
    if (jmptbl4[idx4] == jmptbl_default3)
    {
        uintptr_t ****table = malloc( (1u << JMPTABL_SHIFT3) * sizeof(*table) );
        if (!table) return &jmptbl_oom_entry;
        for (i = 0; i < (1u << JMPTABL_SHIFT3); i++) table[i] = jmptbl_default2;
        if (native_lock_storeifref( &jmptbl4[idx4], table, jmptbl_default3 ) != table) free( table );
    }
    if (jmptbl4[idx4][idx3] == jmptbl_default2)
    {
        uintptr_t ***table = malloc( (1u << JMPTABL_SHIFT2) * sizeof(*table) );
        if (!table) return &jmptbl_oom_entry;
        for (i = 0; i < (1u << JMPTABL_SHIFT2); i++) table[i] = jmptbl_default1;
        if (native_lock_storeifref( &jmptbl4[idx4][idx3], table, jmptbl_default2 ) != table) free( table );
    }
    if (for32bits) return NULL;
    if (jmptbl4[idx4][idx3][idx2] == jmptbl_default1)
    {
        uintptr_t **table = malloc( (1u << JMPTABL_SHIFT1) * sizeof(*table) );
        if (!table) return &jmptbl_oom_entry;
        for (i = 0; i < (1u << JMPTABL_SHIFT1); i++) table[i] = jmptbl_default0;
        if (native_lock_storeifref( &jmptbl4[idx4][idx3][idx2], table, jmptbl_default1 ) != table) free( table );
    }
    if (jmptbl4[idx4][idx3][idx2][idx1] == jmptbl_default0)
    {
        uintptr_t *table = malloc( (1u << JMPTABL_SHIFT0) * sizeof(*table) );
        if (!table) return &jmptbl_oom_entry;
        for (i = 0; i < (1u << JMPTABL_SHIFT0); i++) table[i] = (uintptr_t)native_next;
        if (native_lock_storeifref( &jmptbl4[idx4][idx3][idx2][idx1], table, jmptbl_default0 ) != table)
            free( table );
    }
    return &jmptbl4[idx4][idx3][idx2][idx1][idx0];
}
#else
static uintptr_t *create_jump_table( uintptr_t idx0, uintptr_t idx1, uintptr_t idx2, uintptr_t idx3,
                                     int for32bits )
{
    size_t i;
    if (jmptbl3[idx3] == jmptbl_default2)
    {
        uintptr_t ***table = malloc( (1u << JMPTABL_SHIFT2) * sizeof(*table) );
        if (!table) return &jmptbl_oom_entry;
        for (i = 0; i < (1u << JMPTABL_SHIFT2); i++) table[i] = jmptbl_default1;
        if (native_lock_storeifref( &jmptbl3[idx3], table, jmptbl_default2 ) != table) free( table );
    }
    if (jmptbl3[idx3][idx2] == jmptbl_default1)
    {
        uintptr_t **table = malloc( (1u << JMPTABL_SHIFT1) * sizeof(*table) );
        if (!table) return &jmptbl_oom_entry;
        for (i = 0; i < (1u << JMPTABL_SHIFT1); i++) table[i] = jmptbl_default0;
        if (native_lock_storeifref( &jmptbl3[idx3][idx2], table, jmptbl_default1 ) != table) free( table );
    }
    if (for32bits) return NULL;
    if (jmptbl3[idx3][idx2][idx1] == jmptbl_default0)
    {
        uintptr_t *table = malloc( (1u << JMPTABL_SHIFT0) * sizeof(*table) );
        if (!table) return &jmptbl_oom_entry;
        for (i = 0; i < (1u << JMPTABL_SHIFT0); i++) table[i] = (uintptr_t)native_next;
        if (native_lock_storeifref( &jmptbl3[idx3][idx2][idx1], table, jmptbl_default0 ) != table) free( table );
    }
    return &jmptbl3[idx3][idx2][idx1][idx0];
}
#endif

static uintptr_t *jump_table_entry( uintptr_t addr, int create )
{
    const uintptr_t idx0 = addr & JMPTABLE_MASK0;
    const uintptr_t idx1 = (addr >> JMPTABL_START1) & JMPTABLE_MASK1;
    const uintptr_t idx2 = (addr >> JMPTABL_START2) & JMPTABLE_MASK2;
    const uintptr_t idx3 = (addr >> JMPTABL_START3) & JMPTABLE_MASK3;
#ifdef JMPTABL_SHIFT4
    const uintptr_t idx4 = (addr >> JMPTABL_START4) & JMPTABLE_MASK4;
    if (create) return create_jump_table( idx0, idx1, idx2, idx3, idx4, 0 );
    return &jmptbl4[idx4][idx3][idx2][idx1][idx0];
#else
    if (create) return create_jump_table( idx0, idx1, idx2, idx3, 0 );
    return &jmptbl3[idx3][idx2][idx1][idx0];
#endif
}

int addJumpTableIfDefault64( void *addr, void *jmp )
{
    uintptr_t *entry = jump_table_entry( (uintptr_t)addr, 1 );
    return native_lock_storeifref( entry, jmp, native_next ) == jmp;
}

int setJumpTableIfRef64( void *addr, void *jmp, void *ref )
{
    uintptr_t *entry = jump_table_entry( (uintptr_t)addr, 1 );
    return native_lock_storeifref( entry, jmp, ref ) == jmp;
}

void setJumpTableDefault64( void *addr )
{
    native_lock_store_dd( jump_table_entry( (uintptr_t)addr, 0 ), (uintptr_t)native_next );
}

int setJumpTableDefaultIfRef64( void *addr, void *jmp )
{
    return native_lock_storeifref( jump_table_entry( (uintptr_t)addr, 0 ), native_next, jmp ) == native_next;
}

void setJumpTableDefaultRef64( void *addr, void *jmp )
{
    native_lock_storeifref( jump_table_entry( (uintptr_t)addr, 0 ), native_next, jmp );
}

int isJumpTableDefault64( void *addr )
{
    return *jump_table_entry( (uintptr_t)addr, 0 ) == (uintptr_t)native_next;
}

uintptr_t getJumpTable64(void)
{
#ifdef JMPTABL_SHIFT4
    return (uintptr_t)jmptbl4;
#else
    return (uintptr_t)jmptbl3;
#endif
}

uintptr_t getJumpTable48(void)
{
    return (uintptr_t)jmptbl_48;
}

uintptr_t getJumpTable32(void)
{
#ifdef JMPTABL_SHIFT4
    create_jump_table( 0, 0, 0, 0, 0, 1 );
    return (uintptr_t)jmptbl4[0][0];
#else
    create_jump_table( 0, 0, 0, 0, 1 );
    return (uintptr_t)jmptbl3[0][0];
#endif
}

uintptr_t getJumpTableAddress64( uintptr_t addr )
{
    return (uintptr_t)jump_table_entry( addr, 1 );
}

uintptr_t getJumpAddress64( uintptr_t addr )
{
    return *jump_table_entry( addr, 0 );
}

dynablock_t *getDB( uintptr_t addr )
{
    return *(dynablock_t **)(getJumpAddress64( addr ) - sizeof(void *));
}

int getNeedTest( uintptr_t addr )
{
    const uintptr_t target = getJumpAddress64( addr );
    dynablock_t *block = *(dynablock_t **)(target - sizeof(void *));

    if (!block || target == (uintptr_t)block->block) return 0;
    __atomic_add_fetch( &wine_nx_box64_marked_lookups, 1, __ATOMIC_RELAXED );
    return 1;
}

void *customMalloc( size_t size ) { return malloc( size ); }
void *customMalloc32( size_t size ) { return malloc( size ); }
void *customCalloc( size_t count, size_t size ) { return calloc( count, size ); }
void *customCalloc32( size_t count, size_t size ) { return calloc( count, size ); }
void *customRealloc( void *ptr, size_t size ) { return realloc( ptr, size ); }
void *customRealloc32( void *ptr, size_t size ) { return realloc( ptr, size ); }
void *customMemAligned( size_t align, size_t size ) { return memalign( align, size ); }
void *customMemAligned32( size_t align, size_t size ) { return memalign( align, size ); }
void customFree( void *ptr ) { free( ptr ); }
void customFree32( void *ptr ) { free( ptr ); }

/* Guest addresses are 32-bit. The gate pages hold INT3 sentinels the dynarec
 * would skip over; reporting them non-executable hands them to the
 * interpreter, whose hook ends the run. */
uint32_t getProtection( uintptr_t addr )
{
    unsigned int i, count = __atomic_load_n( &stop_page_count, __ATOMIC_ACQUIRE );

    if (addr > 0xffffffffu) return 0;
    for (i = 0; i < count; i++) if ((addr & ~0xfffu) == stop_pages[i]) return 0;
    return PROT_READ | PROT_EXEC;
}

uint32_t getProtection_fast( uintptr_t addr ) { return getProtection( addr ); }
void protectDB( uintptr_t addr, size_t size ) { (void)addr; (void)size; }
void protectDBJumpTable( uintptr_t addr, size_t size, void *jump, void *ref )
{
    (void)size;
    setJumpTableIfRef64( (void *)addr, jump, ref );
}
void unprotectDB( uintptr_t addr, size_t size, int mark ) { (void)addr; (void)size; (void)mark; }
void neverprotectDB( uintptr_t addr, size_t size, int mark ) { (void)addr; (void)size; (void)mark; }
void unneverprotectDB( uintptr_t addr, size_t size ) { (void)addr; (void)size; }
int isprotectedDB( uintptr_t addr, size_t size ) { (void)addr; (void)size; return 1; }
void CheckHotPage( uintptr_t addr, uint32_t prot ) { (void)addr; (void)prot; }
int isInHotPage( uintptr_t addr ) { (void)addr; return 0; }
int checkInHotPage( uintptr_t addr ) { (void)addr; return 0; }

static size_t lock_address_hash( uintptr_t addr )
{
    addr ^= addr >> 33;
    addr *= UINT64_C(0xff51afd7ed558ccd);
    addr ^= addr >> 33;
    return (size_t)addr & (NX_LOCK_ADDRESS_SLOTS - 1);
}

void addLockAddress( uintptr_t addr )
{
    size_t slot, i;

    if (!addr || lock_addresses_saturated) return;
    slot = lock_address_hash( addr );
    for (i = 0; i < NX_LOCK_ADDRESS_SLOTS; i++)
    {
        uintptr_t current = __atomic_load_n( &lock_addresses[slot], __ATOMIC_ACQUIRE );
        if (current == addr) return;
        if (!current)
        {
            uintptr_t expected = 0;
            if (__atomic_compare_exchange_n( &lock_addresses[slot], &expected, addr, 0,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE ))
                return;
        }
        slot = (slot + 1) & (NX_LOCK_ADDRESS_SLOTS - 1);
    }
    lock_addresses_saturated = 1;
}

int isLockAddress( uintptr_t addr )
{
    size_t slot, i;

    if (!addr) return 0;
    if (lock_addresses_saturated) return 1;
    slot = lock_address_hash( addr );
    for (i = 0; i < NX_LOCK_ADDRESS_SLOTS; i++)
    {
        uintptr_t current = __atomic_load_n( &lock_addresses[slot], __ATOMIC_ACQUIRE );
        if (current == addr) return 1;
        if (!current) return 0;
        slot = (slot + 1) & (NX_LOCK_ADDRESS_SLOTS - 1);
    }
    return 1;
}

int nLockAddressRange( uintptr_t start, size_t size )
{
    int count = 0;
    size_t i;
    for (i = 0; i < NX_LOCK_ADDRESS_SLOTS; i++)
    {
        uintptr_t addr = __atomic_load_n( &lock_addresses[i], __ATOMIC_ACQUIRE );
        if (addr >= start && addr - start < size) count++;
    }
    return count;
}

void getLockAddressRange( uintptr_t start, size_t size, uintptr_t addrs[] )
{
    size_t i, output = 0;
    for (i = 0; i < NX_LOCK_ADDRESS_SLOTS; i++)
    {
        uintptr_t addr = __atomic_load_n( &lock_addresses[i], __ATOMIC_ACQUIRE );
        if (addr >= start && addr - start < size) addrs[output++] = addr;
    }
}

/* Box64 services this backend does not provide: no ELF loader, native
 * wrappers, bridges, tracing, or volatile-range metadata. */
uint64_t rb_inc( rbtree_t *tree, uintptr_t start, uintptr_t end ) { (void)tree; (void)start; (void)end; return 1; }
uint64_t rb_dec( rbtree_t *tree, uintptr_t start, uintptr_t end ) { (void)tree; (void)start; (void)end; return 0; }
uintptr_t rb_get_rightmost( rbtree_t *tree ) { (void)tree; return 0; }
int hasAlternate( void *addr ) { (void)addr; return 0; }
void x64test_step( x64emu_t *ref, uintptr_t ip ) { (void)ref; (void)ip; }
void x64test_check( x64emu_t *ref, uintptr_t ip ) { (void)ref; (void)ip; }
const char *DecodeX64Trace( zydis_dec_t *dec, uintptr_t p, int withhex )
{
    (void)dec; (void)p; (void)withhex; return "";
}
int PrintFunctionAddr( uintptr_t nextaddr, const char *text ) { (void)nextaddr; (void)text; return 0; }
int IsBridgeSignature( char signature, char complement ) { (void)signature; (void)complement; return 0; }
int IsNativeCall( uintptr_t addr, int is32bits, uintptr_t *calladdress, uint16_t *retn )
{
    (void)addr; (void)is32bits;
    if (calladdress) *calladdress = 0;
    if (retn) *retn = 0;
    return 0;
}
const char *GetBridgeName( void *ptr ) { (void)ptr; return NULL; }
const char *GetNativeName( void *ptr ) { (void)ptr; return NULL; }
void *GetNativeFnc( uintptr_t fnc ) { (void)fnc; return NULL; }
int isSimpleWrapper( wrapper_t wrapper ) { (void)wrapper; return 0; }
int isRetX87Wrapper( wrapper_t wrapper ) { (void)wrapper; return 0; }
elfheader_t *FindElfAddress( box64context_t *context, uintptr_t addr ) { (void)context; (void)addr; return NULL; }
int IsAddrFileMapped( uintptr_t addr, const char **filename, uintptr_t *start )
{
    (void)addr;
    if (filename) *filename = NULL;
    if (start) *start = 0;
    return 0;
}
size_t SizeFileMapped( uintptr_t addr ) { (void)addr; return 0; }
int IsAddrElfOrFileMapped( uintptr_t addr ) { (void)addr; return 0; }
int VolatileRangesContains( uintptr_t addr ) { (void)addr; return 0; }
int VolatileOpcodesHas( uintptr_t addr ) { (void)addr; return 0; }
int is_addr_unaligned( uintptr_t addr ) { (void)addr; return 0; }
/* A 64-bit SYSCALL instruction cannot occur in 32-bit guest code. */
void EmuX64Syscall_linux( void *emu ) { EmuX64Syscall( emu ); }
int is_addr_autosmc( uintptr_t addr ) { (void)addr; return 0; }
int IsAddrNeedReloc( uintptr_t addr ) { (void)addr; return 0; }
box64env_t *GetCurEnvByAddr( uintptr_t addr ) { (void)addr; return &box64env; }
void writePerfMap( uintptr_t func_addr, uintptr_t code_addr, size_t code_size, const char *inst_name )
{
    (void)func_addr; (void)code_addr; (void)code_size; (void)inst_name;
}
int SchedYield(void)
{
#ifdef __SWITCH__
    svcSleepThread( 0 );
    return 0;
#else
    return sched_yield();
#endif
}

#ifdef __SWITCH__
/* Box64 masks asynchronous POSIX signals around translation on Linux.
 * Horizon has no such signal delivery; synchronous exceptions remain enabled
 * and the engine boundary cancels translation while holding its lock. */
int wine_nx_box64_sigmask( int how, const sigset_t *set, sigset_t *old )
{
    (void)how;
    (void)set;
    if (old) memset( old, 0, sizeof(*old) );
    return 0;
}
#endif
