/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include <fenv.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __SWITCH__
#include <switch/arm/counter.h>
#endif
#include "wow64_box64_engine.h"
#include "../../dlls/winebox64/cpuid.h"
#include "box64context.h"
#include "box64cpu.h"
#include "debug.h"
#include "emit_signals.h"
#include "freq.h"
#include "my_cpuid.h"
#include "x64emu.h"
#include "x64emu_private.h"
#include "x87emu_private.h"
#include "x64_signals.h"

struct nx_engine
{
    x64emu_t emu;
#ifdef __SWITCH__
    jmp_buf escape;
#else
    sigjmp_buf escape;
#endif
    const struct wine_nx_wow64_gates *gates;
    const struct wine_nx_wow64_host *host;
    void *opaque;
    ULONG fs_base, completion;
    ULONG code_page; /* page of the last checked instruction fetch; 1 = none */
    ULONGLONG remaining, executed;
    NTSTATUS status;
    pthread_mutex_t *held_mutex;
};

static __thread struct nx_engine *active_engine;
/* Engines are per run and freed before any gate is dispatched, so a thread
 * exiting through NtTerminateThread owns none. Reported by the lifecycle log. */
LONG wine_nx_box64_live_engines;
/* Throughput, logged periodically by the runtime. */
ULONGLONG wine_nx_box64_executed_total, wine_nx_box64_runs_total;

/* The first fetch from each code page goes through the checked host read;
 * later bytes on it are read directly. A page unmapped meanwhile faults inside
 * Run and unwinds through the same boundary as an operand fault. Checked reads
 * are Wine __TRY frames on Horizon and cost far more than the instruction. */
static NTSTATUS fetch_code_byte( struct nx_engine *engine, ULONG address, unsigned char *byte )
{
    NTSTATUS status;

    if ((address & ~0xfffu) == engine->code_page)
    {
        *byte = *(volatile const unsigned char *)(uintptr_t)address;
        return STATUS_SUCCESS;
    }
    if ((status = engine->host->read( engine->opaque, address, byte, 1 ))) return status;
    engine->code_page = address & ~0xfffu;
    return STATUS_SUCCESS;
}
static box64context_t core_context = { .mutex_lock = PTHREAD_MUTEX_INITIALIZER };
box64context_t *my_context = &core_context;
/* No Linux environment/loader initialization. Do not advertise optional CPU
 * features before the corresponding context and exception paths are supported. */
box64env_t box64env;
int box64_wine = 1;
int box64_unittest_mode;
uint8_t box64_rdtsc_shift;

static void stop_engine( x64emu_t *emu, NTSTATUS status )
{
    struct nx_engine *engine = (struct nx_engine *)emu;
    engine->status = status;
#ifdef __SWITCH__
    longjmp( engine->escape, 1 );
#else
    /* A host SIGSEGV/SIGBUS handler may enter this boundary. Restore its signal
     * mask as well, otherwise a second guest fault would terminate the host. */
    siglongjmp( engine->escape, 1 );
#endif
}

BOOL wine_nx_box64_handle_fault( ULONG_PTR address )
{
    if (!active_engine || address > 0xffffffffu) return FALSE;
    stop_engine( &active_engine->emu, STATUS_ACCESS_VIOLATION );
    return TRUE;
}

/* Only interpreter objects redirect pthread mutex calls here. A memory XCHG
 * or LOCK instruction can fault while holding this mutex; release it back on
 * the normal stack after the fault boundary unwinds. No PE callbacks execute
 * while active_engine points at the engine holding the mutex. */
int wine_nx_box64_mutex_lock( pthread_mutex_t *mutex )
{
    int ret = pthread_mutex_lock( mutex );
    if (!ret && active_engine) active_engine->held_mutex = mutex;
    return ret;
}

int wine_nx_box64_mutex_unlock( pthread_mutex_t *mutex )
{
    int ret = pthread_mutex_unlock( mutex );
    if (!ret && active_engine && active_engine->held_mutex == mutex)
        active_engine->held_mutex = NULL;
    return ret;
}

/* Hook added to a generated copy of the pinned interpreter. Called before each
 * main instruction, including before any guest gate bytes are fetched. */
int wine_nx_box64_before_instruction( x64emu_t *emu, uintptr_t pc )
{
    struct nx_engine *engine = (struct nx_engine *)emu;
    unsigned char opcode = 0;
    unsigned int prefix;
    NTSTATUS status;
    emu->ip.q[0] = pc;
    if (emu->segs[_CS] != 0x23 || pc > 0xffffffffu)
        stop_engine( emu, STATUS_NOT_SUPPORTED );
    if (pc == engine->gates->syscall || pc == engine->gates->unix_call ||
        (engine->completion && pc == engine->completion)) return 1;
    if (!engine->remaining) stop_engine( emu, STATUS_TIMEOUT );
    CheckExec( emu, pc );
    /* Reject state families not represented by this initial adapter before
     * executing them, including when an instruction has legacy prefixes. */
    for (prefix = 0; prefix < 15; ++prefix)
    {
        if (pc + prefix > 0xffffffffu) stop_engine( emu, STATUS_ACCESS_VIOLATION );
        status = fetch_code_byte( engine, pc + prefix, &opcode );
        if (status) stop_engine( emu, status );
        if (opcode != 0x26 && opcode != 0x2e && opcode != 0x36 && opcode != 0x3e &&
            opcode != 0x64 && opcode != 0x65 && opcode != 0x66 && opcode != 0x67 &&
            opcode != 0xf0 && opcode != 0xf2 && opcode != 0xf3) break;
    }
    if (prefix == 15) stop_engine( emu, STATUS_ILLEGAL_INSTRUCTION );
    /* VEX (AVX) is not advertised; LES/LDS share these opcodes in 32-bit mode. */
    if (opcode == 0xc4 || opcode == 0xc5) stop_engine( emu, STATUS_NOT_SUPPORTED );
    --engine->remaining;
    ++engine->executed;
    return 0;
}

void CheckExec( x64emu_t *emu, uintptr_t pc )
{
    struct nx_engine *engine = (struct nx_engine *)emu;
    unsigned char byte;
    NTSTATUS status;
    if (pc > 0xffffffffu) stop_engine( emu, STATUS_ACCESS_VIOLATION );
    if (pc == engine->gates->syscall || pc == engine->gates->unix_call ||
        (engine->completion && pc == engine->completion)) return;
    status = fetch_code_byte( engine, pc, &byte );
    if (status) stop_engine( emu, status );
}

void EmitSignal( x64emu_t *emu, int sig, void *addr, int code )
{
    (void)addr; (void)code;
    stop_engine( emu, sig == X64_SIGTRAP ? STATUS_BREAKPOINT :
                      sig == X64_SIGSEGV ? STATUS_ACCESS_VIOLATION : STATUS_ILLEGAL_INSTRUCTION );
}
void EmitDiv0( x64emu_t *emu, void *addr, int code )
{
    (void)addr; (void)code; stop_engine( emu, STATUS_INTEGER_DIVIDE_BY_ZERO );
}
void EmitInterruption( x64emu_t *emu, int num, void *addr )
{
    (void)num; (void)addr; stop_engine( emu, STATUS_ILLEGAL_INSTRUCTION );
}
void EmuX64Syscall( void *emu ) { stop_engine( emu, STATUS_NOT_SUPPORTED ); }
void EmuX86Syscall( void *emu ) { stop_engine( emu, STATUS_NOT_SUPPORTED ); }
void EmuInt3( void *emu, void *addr ) { (void)addr; stop_engine( emu, STATUS_BREAKPOINT ); }
void *EmuFork( void *emu, int type )
{
    (void)type; stop_engine( emu, STATUS_NOT_SUPPORTED ); return NULL;
}
void *getAlternate( void *address ) { return address; }
int GetTID(void) { return 0; } /* interpreter diagnostics only */
void PrintfFtrace( int prefix, const char *format, ... )
{
    va_list args;
    (void)prefix;
    va_start( args, format ); vfprintf( stderr, format, args ); va_end( args );
}
void *GetSegmentBase( void *opaque, uint32_t selector )
{
    struct nx_engine *engine = opaque;
    if (selector == engine->emu.segs[_FS]) return (void *)(uintptr_t)engine->fs_base;
    if (selector == engine->emu.segs[_GS]) return NULL;
    stop_engine( opaque, STATUS_NOT_SUPPORTED );
    return NULL;
}
void *GetSeg43Base( void *emu ) { stop_engine( emu, STATUS_NOT_SUPPORTED ); return NULL; }

/* The CPU identity is shared with winebox64 (dlls/winebox64/cpuid.h), so CPUID,
 * IsProcessorFeaturePresent and GetSystemInfo describe the same processor. */
void my_cpuid( x64emu_t *emu, uint32_t leaf )
{
    static const char vendor[12] = {'G','e','n','u','i','n','e','I','n','t','e','l'};
    static const char brand[48] = "Wine-NX Box64 i386 interpreter";
    uint32_t regs[4] = {0}; /* eax, ebx, ecx, edx */

    switch (leaf)
    {
    case 0:
        regs[0] = 1;
        memcpy( &regs[1], vendor, 4 );
        memcpy( &regs[3], vendor + 4, 4 );
        memcpy( &regs[2], vendor + 8, 4 );
        break;
    case 1:
        regs[0] = WINEBOX64_CPUID_SIGNATURE;
        regs[1] = (1u << 16) | (8u << 8); /* one logical CPU, 64-byte CLFLUSH line */
        regs[3] = WINEBOX64_CPUID_EDX;
        break;
    case 0x80000000:
        regs[0] = 0x80000004;
        break;
    case 0x80000002: case 0x80000003: case 0x80000004:
        memcpy( regs, brand + (leaf - 0x80000002) * 16, 16 );
        break;
    default:
        break;
    }
    emu->regs[_AX].q[0] = regs[0];
    emu->regs[_BX].q[0] = regs[1];
    emu->regs[_CX].q[0] = regs[2];
    emu->regs[_DX].q[0] = regs[3];
}
uint32_t helper_getcpu( x64emu_t *emu ) { stop_engine( emu, STATUS_NOT_SUPPORTED ); return 0; }

/* A monotonic nanosecond count; CPUID advertises TSC. */
uint64_t ReadTSC( x64emu_t *emu )
{
    (void)emu;
#ifdef __SWITCH__
    return armTicksToNs( armGetSystemTick() );
#else
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
#endif
}
uint32_t get_random32(void) { stop_engine( &active_engine->emu, STATUS_NOT_SUPPORTED ); return 0; }
uint64_t get_random64(void) { stop_engine( &active_engine->emu, STATUS_NOT_SUPPORTED ); return 0; }

/* x87, MMX and SSE state crosses runs in the context's FXSAVE area, in the
 * layout Box64 also uses for the guest's own FXSAVE/FXRSTOR instructions:
 * registers as doubles, tags relative to TOP. The FNSAVE area (FloatSave) gets
 * an architectural image with 80-bit registers for other readers; it is not
 * read back. */
static void import_fpu( x64emu_t *emu, const I386_CONTEXT *ctx )
{
    XMM_SAVE_AREA32 fx;
    unsigned int depth = 0;

    memcpy( &fx, ctx->ExtendedRegisters, sizeof(fx) );
    fpu_fxrstor32( emu, &fx );
    /* FXRSTOR leaves Box64's push counter alone; FXAM consults it. */
    while (depth < 8 && !((emu->fpu_tags >> (2 * depth)) & 3)) depth++;
    emu->fpu_stack = depth;
}

static void export_fpu( x64emu_t *emu, I386_CONTEXT *ctx )
{
    XMM_SAVE_AREA32 fx;
    unsigned int i, top = emu->top & 7, tags = 0;

    memcpy( &fx, ctx->ExtendedRegisters, sizeof(fx) );
    fpu_fxsave32( emu, &fx );
    memcpy( ctx->ExtendedRegisters, &fx, sizeof(fx) );

    ctx->FloatSave.ControlWord = emu->cw.x16;
    ctx->FloatSave.StatusWord = emu->sw.x16; /* fxsave32 stored TOP in it */
    ctx->FloatSave.ErrorOffset = ctx->FloatSave.ErrorSelector = 0;
    ctx->FloatSave.DataOffset = ctx->FloatSave.DataSelector = 0;
    memset( ctx->FloatSave.RegisterArea, 0, sizeof(ctx->FloatSave.RegisterArea) );
    for (i = 0; i < 8; i++)
    {
        unsigned int empty = (emu->fpu_tags >> (2 * i)) & 3;
        /* FNSAVE tags are indexed by physical register, ST(i) = R((TOP + i) & 7). */
        tags |= (empty ? 3u : 0u) << (2 * ((top + i) & 7));
        if (!empty) D2LD( &emu->x87[(top + i) & 7].d, &ctx->FloatSave.RegisterArea[i * 10] );
    }
    ctx->FloatSave.TagWord = tags;
}

static NTSTATUS import_context( struct nx_engine *engine, const I386_CONTEXT *ctx )
{
    x64emu_t *emu = &engine->emu;
    /* Only the low 16 bits of a selector field are meaningful. */
    if ((WORD)ctx->SegCs != 0x23 || (ctx->ContextFlags & CONTEXT_I386_XSTATE) == CONTEXT_I386_XSTATE)
        return STATUS_NOT_SUPPORTED;
    emu->regs[_AX].q[0] = ctx->Eax; emu->regs[_BX].q[0] = ctx->Ebx;
    emu->regs[_CX].q[0] = ctx->Ecx; emu->regs[_DX].q[0] = ctx->Edx;
    emu->regs[_SI].q[0] = ctx->Esi; emu->regs[_DI].q[0] = ctx->Edi;
    emu->regs[_SP].q[0] = ctx->Esp; emu->regs[_BP].q[0] = ctx->Ebp;
    emu->ip.q[0] = ctx->Eip; emu->eflags.x64 = ctx->EFlags;
    emu->df = d_none;
    emu->segs[_CS] = (WORD)ctx->SegCs; emu->segs[_SS] = (WORD)ctx->SegSs;
    emu->segs[_DS] = (WORD)ctx->SegDs; emu->segs[_ES] = (WORD)ctx->SegEs;
    emu->segs[_FS] = (WORD)ctx->SegFs; emu->segs[_GS] = (WORD)ctx->SegGs;
    emu->segs_offs[_FS] = engine->fs_base;
    import_fpu( emu, ctx );
    return STATUS_SUCCESS;
}

static NTSTATUS export_context( struct nx_engine *engine, I386_CONTEXT *ctx )
{
    x64emu_t *emu = &engine->emu;
    UpdateFlags( emu );
    ctx->Eax = emu->regs[_AX].dword[0]; ctx->Ebx = emu->regs[_BX].dword[0];
    ctx->Ecx = emu->regs[_CX].dword[0]; ctx->Edx = emu->regs[_DX].dword[0];
    ctx->Esi = emu->regs[_SI].dword[0]; ctx->Edi = emu->regs[_DI].dword[0];
    ctx->Esp = emu->regs[_SP].dword[0]; ctx->Ebp = emu->regs[_BP].dword[0];
    ctx->Eip = emu->ip.dword[0]; ctx->EFlags = emu->eflags.x64;
    ctx->SegCs = emu->segs[_CS]; ctx->SegSs = emu->segs[_SS];
    ctx->SegDs = emu->segs[_DS]; ctx->SegEs = emu->segs[_ES];
    ctx->SegFs = emu->segs[_FS]; ctx->SegGs = emu->segs[_GS];
    export_fpu( emu, ctx );
    return STATUS_SUCCESS;
}

NTSTATUS wine_nx_box64_run( I386_CONTEXT *context, ULONG fs_base,
                          const struct wine_nx_wow64_gates *gates,
                          const struct wine_nx_wow64_host *host, void *opaque,
                          ULONG completion_pc, ULONGLONG budget, ULONGLONG *executed )
{
    static uint32_t parity[8] = {0x96696996,0x69969669,0x69969669,0x96696996,
                                0x69969669,0x96696996,0x96696996,0x69969669};
    struct nx_engine *engine, *previous = active_engine;
    NTSTATUS status;
    fenv_t native_fenv;
    int i;
    if (executed) *executed = 0;
    if (!context || !gates || !host || !host->read || !gates->syscall ||
        !gates->unix_call || gates->syscall == gates->unix_call || !budget ||
        completion_pc == gates->syscall || completion_pc == gates->unix_call)
        return STATUS_INVALID_PARAMETER;
    if (!(engine = calloc( 1, sizeof(*engine) ))) return STATUS_NO_MEMORY;
    __atomic_add_fetch( &wine_nx_box64_live_engines, 1, __ATOMIC_RELAXED );
    engine->gates = gates; engine->host = host; engine->opaque = opaque;
    engine->fs_base = fs_base; engine->completion = completion_pc; engine->remaining = budget;
    engine->code_page = 1;
    engine->emu.context = &core_context;
    engine->emu.x64emu_parity_tab = parity;
    for (i = 0; i < 16; ++i) engine->emu.sbiidx[i] = &engine->emu.regs[i];
    engine->emu.sbiidx[4] = &engine->emu.zero;
    reset_fpu( &engine->emu );
    for (;;)
    {
        status = import_context( engine, context );
        if (status) break;
        fegetenv( &native_fenv );
        active_engine = engine;
#ifdef __SWITCH__
        if (!setjmp( engine->escape )) Run( &engine->emu, 0 );
#else
        if (!sigsetjmp( engine->escape, 1 )) Run( &engine->emu, 0 );
#endif
        active_engine = previous;
        if (engine->held_mutex)
        {
            pthread_mutex_unlock( engine->held_mutex );
            engine->held_mutex = NULL;
        }
        fesetenv( &native_fenv );
        status = export_context( engine, context );
        if (engine->status) status = engine->status;
        if (status || (completion_pc && context->Eip == completion_pc)) break;
        /* No callbacks: hand the saved context back across the PE/Unix boundary. */
        if (!host->syscall && !host->unix_call) break;
        status = wine_nx_wow64_dispatch_gate( context, gates, host, opaque );
        if (status) break;
    }
    if (executed) *executed = engine->executed;
    __atomic_add_fetch( &wine_nx_box64_executed_total, engine->executed, __ATOMIC_RELAXED );
    __atomic_add_fetch( &wine_nx_box64_runs_total, 1, __ATOMIC_RELAXED );
    free( engine );
    __atomic_sub_fetch( &wine_nx_box64_live_engines, 1, __ATOMIC_RELAXED );
    return status;
}
