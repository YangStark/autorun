#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int BOOL;
typedef uint32_t ULONG;
typedef uint64_t ULONG_PTR;
typedef void (*PRTL_THREAD_START_ROUTINE)(void *, void *);

#define FALSE 0
#define TRUE 1
#define CONTEXT_ARM64_FULL 0x00400007
#define CONTEXT_EXCEPTION_ACTIVE 0x08000000
#define CONTEXT_EXCEPTION_REPORTING 0x80000000
#define DECLSPEC_NORETURN __attribute__((noreturn))
#define X0 X[0]
#define X1 X[1]
#define X18 X[18]

typedef struct __attribute__((aligned(16)))
{
    ULONG ContextFlags;
    ULONG Cpsr;
    uint64_t X[31];
    uint64_t Sp;
    uint64_t Pc;
    unsigned char tail[0x280];
} CONTEXT;

typedef struct _NT_TIB
{
    void *ExceptionList;
    void *StackBase;
    void *StackLimit;
    void *SubSystemTib;
    void *FiberData;
    void *ArbitraryUserPointer;
    struct _NT_TIB *Self;
} NT_TIB;

typedef struct
{
    NT_TIB Tib;
    unsigned char reserved[0x1788 - sizeof(NT_TIB)];
    void *ChpeV2CpuAreaInfo;
} TEB;

_Static_assert( sizeof(CONTEXT) == 0x390, "ARM64 CONTEXT size" );
_Static_assert( offsetof(CONTEXT, Sp) == 0x100, "ARM64 CONTEXT Sp" );
_Static_assert( offsetof(CONTEXT, Pc) == 0x108, "ARM64 CONTEXT Pc" );
_Static_assert( offsetof(TEB, ChpeV2CpuAreaInfo) == 0x1788, "TEB CPU area" );

static void *pRtlUserThreadStart;
static void *pLdrInitializeThunk;
static jmp_buf resume_jump;
static CONTEXT continued, wait_seen;
static TEB *active_teb;
static unsigned int event_number, active_event, wait_event, continue_event;
static unsigned int active_count, wait_count, continue_count, sleep_count;
static int mutate_wait, pristine_before_wait;
static uintptr_t wait_mutated_sp;
static unsigned char *pristine_address;
static size_t pristine_size;
static unsigned int failures;

static int memory_is( const unsigned char *memory, size_t size, unsigned char value )
{
    size_t i;

    for (i = 0; i < size; ++i) if (memory[i] != value) return 0;
    return 1;
}

void wine_nx_set_active_pe_teb( TEB *teb )
{
    active_teb = teb;
    active_count++;
    active_event = ++event_number;
}

void wait_suspend( CONTEXT *context )
{
    wait_seen = *context;
    wait_count++;
    wait_event = ++event_number;
    pristine_before_wait = memory_is( pristine_address, pristine_size, 0xa5 );
    if (!mutate_wait) return;
    context->X0 = 0x1010101010101010ull;
    context->X1 = 0x1111111111111111ull;
    context->X18 = 0x1818181818181818ull;
    context->Sp = wait_mutated_sp;
    context->Pc = 0x2020202020202020ull;
}

void horizon_continue_context( const CONTEXT *context )
{
    continued = *context;
    continue_count++;
    continue_event = ++event_number;
    longjmp( resume_jump, 1 );
}

unsigned int sleep( unsigned int seconds )
{
    (void)seconds;
    sleep_count++;
    abort();
}

#include "extracted_startup.inc"

static void report( const char *name, int passed )
{
    printf( "%s %s\n", passed ? "PASS" : "FAIL", name );
    failures += !passed;
}

static void reset_mocks(void)
{
    memset( &continued, 0, sizeof(continued) );
    memset( &wait_seen, 0, sizeof(wait_seen) );
    active_teb = NULL;
    event_number = active_event = wait_event = continue_event = 0;
    active_count = wait_count = continue_count = sleep_count = 0;
    mutate_wait = pristine_before_wait = 0;
    wait_mutated_sp = 0;
    pristine_address = NULL;
    pristine_size = 0;
}

static int run_startup( PRTL_THREAD_START_ROUTINE entry, void *arg, BOOL suspend, TEB *teb )
{
    if (!setjmp( resume_jump )) wine_nx_start_arm64ec_thread( entry, arg, suspend, teb );
    return continue_count == 1;
}

static void initialize_teb( TEB *teb, void *stack_base, void *cpu_area )
{
    memset( teb, 0x3c, sizeof(*teb) );
    teb->Tib.StackBase = stack_base;
    teb->ChpeV2CpuAreaInfo = cpu_area;
}

static void test_direct_start(void)
{
    _Alignas(16) unsigned char stack[0x1400];
    unsigned char cpu_area[0x100], cpu_snapshot[sizeof(cpu_area)];
    TEB teb, teb_snapshot;
    PRTL_THREAD_START_ROUTINE entry = (PRTL_THREAD_START_ROUTINE)(uintptr_t)0x123456789abc0000ull;
    void *arg = (void *)(uintptr_t)0x23456789abcd0000ull;
    uintptr_t initial_sp, saved_address;
    CONTEXT *saved;

    memset( stack, 0xa5, sizeof(stack) );
    memset( cpu_area, 0x5a, sizeof(cpu_area) );
    initialize_teb( &teb, stack + sizeof(stack) - 3, cpu_area );
    memcpy( &teb_snapshot, &teb, sizeof(teb) );
    memcpy( cpu_snapshot, cpu_area, sizeof(cpu_area) );
    reset_mocks();

    initial_sp = (uintptr_t)teb.Tib.StackBase & ~(uintptr_t)15;
    saved_address = initial_sp - sizeof(CONTEXT);
    report( "direct-nonreturn-resume", run_startup( entry, arg, FALSE, &teb ) );
    saved = (CONTEXT *)saved_address;
    report( "direct-callback-counts", active_count == 1 && !wait_count && continue_count == 1 );
    report( "direct-order", active_event == 1 && continue_event == 2 );
    if (active_teb != &teb) printf( "active TEB %p expected %p\n", (void *)active_teb, (void *)&teb );
    report( "direct-active-teb", active_teb == &teb );
    report( "direct-no-fallthrough", !sleep_count );
    report( "direct-saved-placement", saved_address % 16 == 0 &&
            saved->Sp == initial_sp && saved->Pc == (uintptr_t)pRtlUserThreadStart );
    report( "direct-saved-arguments", saved->ContextFlags == CONTEXT_ARM64_FULL &&
            saved->X0 == (uintptr_t)entry && saved->X1 == (uintptr_t)arg &&
            saved->X18 == (uintptr_t)&teb );
    report( "direct-loader-context", continued.ContextFlags == CONTEXT_ARM64_FULL &&
            continued.Pc == (uintptr_t)pLdrInitializeThunk &&
            continued.Sp == saved_address && continued.X0 == saved_address &&
            continued.X1 == (uintptr_t)arg && continued.X18 == (uintptr_t)&teb );
    report( "direct-cpu-area-untouched", !memcmp( cpu_area, cpu_snapshot, sizeof(cpu_area) ) &&
            !memcmp( &teb, &teb_snapshot, sizeof(teb) ) );
}

static void test_suspended_start(void)
{
    _Alignas(16) unsigned char stack[0x1800];
    unsigned char cpu_area[0x100], cpu_snapshot[sizeof(cpu_area)];
    TEB teb, teb_snapshot;
    PRTL_THREAD_START_ROUTINE entry = (PRTL_THREAD_START_ROUTINE)(uintptr_t)0x3456789abcde0000ull;
    void *arg = (void *)(uintptr_t)0x456789abcdef0000ull;
    uintptr_t original_sp, original_saved, changed_saved;
    CONTEXT *saved;

    memset( stack, 0xa5, sizeof(stack) );
    memset( cpu_area, 0x6b, sizeof(cpu_area) );
    initialize_teb( &teb, stack + sizeof(stack) - 5, cpu_area );
    memcpy( &teb_snapshot, &teb, sizeof(teb) );
    memcpy( cpu_snapshot, cpu_area, sizeof(cpu_area) );
    reset_mocks();

    original_sp = (uintptr_t)teb.Tib.StackBase & ~(uintptr_t)15;
    original_saved = original_sp - sizeof(CONTEXT);
    wait_mutated_sp = (uintptr_t)(stack + 0xc37);
    changed_saved = (wait_mutated_sp & ~(uintptr_t)15) - sizeof(CONTEXT);
    pristine_address = (unsigned char *)original_saved;
    pristine_size = sizeof(CONTEXT);
    mutate_wait = 1;

    report( "suspend-nonreturn-resume", run_startup( entry, arg, TRUE, &teb ) );
    saved = (CONTEXT *)changed_saved;
    report( "suspend-order", active_count == 1 && wait_count == 1 && continue_count == 1 &&
            active_event == 1 && wait_event == 2 && continue_event == 3 && !sleep_count );
    report( "suspend-reported-context", wait_seen.ContextFlags ==
            (CONTEXT_ARM64_FULL | CONTEXT_EXCEPTION_REPORTING | CONTEXT_EXCEPTION_ACTIVE) &&
            wait_seen.X0 == (uintptr_t)entry && wait_seen.X1 == (uintptr_t)arg &&
            wait_seen.X18 == (uintptr_t)&teb && wait_seen.Sp == original_sp &&
            wait_seen.Pc == (uintptr_t)pRtlUserThreadStart );
    report( "suspend-wait-before-save", pristine_before_wait &&
            memory_is( (unsigned char *)original_saved, sizeof(CONTEXT), 0xa5 ) );
    report( "suspend-changed-save-placement", changed_saved % 16 == 0 &&
            saved->Sp == wait_mutated_sp && saved->Pc == 0x2020202020202020ull );
    report( "suspend-changed-context-saved", saved->ContextFlags == CONTEXT_ARM64_FULL &&
            saved->X0 == 0x1010101010101010ull && saved->X1 == 0x1111111111111111ull &&
            saved->X18 == 0x1818181818181818ull );
    report( "suspend-loader-context", continued.ContextFlags == CONTEXT_ARM64_FULL &&
            continued.Pc == (uintptr_t)pLdrInitializeThunk &&
            continued.Sp == changed_saved && continued.X0 == changed_saved &&
            continued.X1 == 0x1111111111111111ull && continued.X18 == 0x1818181818181818ull );
    report( "suspend-cpu-area-untouched", !memcmp( cpu_area, cpu_snapshot, sizeof(cpu_area) ) &&
            !memcmp( &teb, &teb_snapshot, sizeof(teb) ) );
}

int main(void)
{
    pRtlUserThreadStart = (void *)(uintptr_t)0x7070707070707070ull;
    pLdrInitializeThunk = (void *)(uintptr_t)0x7171717171717171ull;
    test_direct_start();
    test_suspended_start();
    printf( "%u startup test failure(s)\n", failures );
    return failures != 0;
}
