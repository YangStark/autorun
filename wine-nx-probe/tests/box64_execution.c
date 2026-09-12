/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#ifdef WINE_NX_BOX64_DYNAREC
#define COUNT_IS(actual, expected) ((void)(actual), 1)
#else
#define COUNT_IS(actual, expected) ((actual) == (expected))
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef __SWITCH__
#include <malloc.h>
#include <stdlib.h>
#include <switch.h>
#else
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#endif
#include "wow64_box64_engine.h"
#ifdef __SWITCH__
#undef far /* Wine poisons this identifier; libnx uses it for the fault register. */
u32 __nx_exception_ignoredebug = 1;
#endif

#define BASE 0x10000000u
#define SIZE 0x10000u
static volatile int native_faults;
#ifdef __SWITCH__
unsigned char __attribute__((aligned(16))) __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler( ThreadExceptionDump *ctx )
{
    unsigned int exception_class = ctx->esr >> 26;
    if ((exception_class == 0x24 || exception_class == 0x25) && !(ctx->esr & (1u << 10)))
    {
        ++native_faults;
        wine_nx_box64_handle_fault( ctx->far.x );
    }
    printf( "Unexpected native exception esr=%#x pc=%#llx far=%#llx\n", ctx->esr,
            (unsigned long long)ctx->pc.x, (unsigned long long)ctx->far.x );
    consoleUpdate( NULL );
    for (;;) svcSleepThread( 1000000000ULL );
}

static void *backing;
static void *allocate_guest(void)
{
    Result rc;
    if (!(backing = memalign( 0x1000, SIZE ))) return NULL;
    rc = svcMapProcessCodeMemory( envGetOwnProcessHandle(), BASE, (u64)backing, SIZE );
    if (R_SUCCEEDED(rc))
    {
        rc = svcSetProcessMemoryPermission( envGetOwnProcessHandle(), BASE, SIZE, Perm_Rw );
        if (R_SUCCEEDED(rc)) return (void *)(uintptr_t)BASE;
        svcUnmapProcessCodeMemory( envGetOwnProcessHandle(), BASE, (u64)backing, SIZE );
    }
    printf( "Guest mapping at %#x failed: %#x\n", BASE, rc );
    free( backing ); backing = NULL;
    return NULL;
}
static int release_guest(void *memory)
{
    Result rc = svcUnmapProcessCodeMemory( envGetOwnProcessHandle(), (u64)memory, (u64)backing, SIZE );
    if (R_FAILED(rc)) return -1;
    free( backing ); backing = NULL;
    return 0;
}
static int protect_fault_page( BOOL accessible )
{
    return R_FAILED(svcSetMemoryPermission( (void *)(uintptr_t)(BASE + SIZE - 0x1000),
                                            0x1000, accessible ? Perm_Rw : Perm_None ));
}
#else
static void operand_fault_handler( int signal, siginfo_t *info, void *context )
{
    (void)context;
    ++native_faults;
    wine_nx_box64_handle_fault( (ULONG_PTR)info->si_addr );
    _Exit( 128 + signal ); /* Never consume a fault outside an active guest run. */
}

static void *allocate_guest(void)
{
    return mmap( (void *)(uintptr_t)BASE, SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0 );
}
static int release_guest(void *memory) { return munmap( memory, SIZE ); }
static int protect_fault_page( BOOL accessible )
{
    return mprotect( (void *)(uintptr_t)(BASE + SIZE - 0x1000), 0x1000,
                     accessible ? PROT_READ | PROT_WRITE : PROT_NONE );
}
#endif
struct fixture
{
    I386_CONTEXT *context;
    struct wine_nx_wow64_gates gates;
    unsigned int calls;
    BOOL nested;
};
static unsigned int guest_reads;
static NTSTATUS read_guest( void *opaque, ULONG address, void *buffer, SIZE_T size )
{
    (void)opaque;
    guest_reads++;
    if (address < BASE || address - BASE >= SIZE || size > SIZE - (address - BASE))
        return STATUS_ACCESS_VIOLATION;
    memcpy( buffer, (void *)(uintptr_t)address, size );
    return STATUS_SUCCESS;
}
#ifdef WINE_NX_BOX64_DYNAREC
extern void wine_nx_box64_invalidate( uintptr_t address, size_t size, int destroy );
extern unsigned int wine_nx_box64_block_tests;
#endif
/* Translated code is only revalidated when a change is reported, as winebox64
 * does for NtFlushInstructionCache, so code written over code is reported. */
static void put_code( unsigned char *memory, ULONG offset, const unsigned char *code, size_t size )
{
    memcpy( memory + offset, code, size );
#ifdef WINE_NX_BOX64_DYNAREC
    wine_nx_box64_invalidate( BASE + offset, size, 0 );
#endif
}
static void init_context( I386_CONTEXT *ctx, ULONG pc, ULONG sp )
{
    XMM_SAVE_AREA32 fx = {0};
    memset( ctx, 0, sizeof(*ctx) );
    ctx->ContextFlags = CONTEXT_I386_ALL;
    ctx->Eip = pc; ctx->Esp = sp; ctx->EFlags = 0x202;
    ctx->SegCs = 0x23; ctx->SegSs = ctx->SegDs = ctx->SegEs = 0x2b; ctx->SegFs = 0x53;
    ctx->FloatSave.ControlWord = 0x37f; ctx->FloatSave.TagWord = 0xffff;
    fx.ControlWord = 0x37f; fx.MxCsr = 0x1f80;
    memcpy( ctx->ExtendedRegisters, &fx, sizeof(fx) );
}
static NTSTATUS native_call( void *opaque, ULONG number, ULONG arguments );
static NTSTATUS native_unix_call( void *opaque, ULONGLONG handle, ULONG code, ULONG arguments )
{
    struct fixture *f = opaque;
    assert( handle == 0xfedcba9876543210ULL && code == 17 && arguments == 0x10203040 );
    f->calls++;
    f->context->Edi = 0xbadc0de;
    return STATUS_INVALID_HANDLE;
}
static const struct wine_nx_wow64_host host = {read_guest, native_call, native_unix_call, NULL};

/* Decode a normal 80-bit x87 value (FNSAVE RegisterArea) without long double. */
static double ld80_to_double( const unsigned char *p )
{
    ULONGLONG mantissa;
    int exponent = (p[8] | p[9] << 8) & 0x7fff;
    memcpy( &mantissa, p, 8 );
    if (!exponent && !mantissa) return 0.0;
    return ((p[9] & 0x80) ? -1.0 : 1.0) * ldexp( (double)mantissa, exponent - 16383 - 63 );
}

/* At the gate the exported context must describe the live x87 stack: control
 * word, TOP, physical-register tags and 80-bit ST(0)/ST(1). The callback then
 * replaces ST(0) in the FXSAVE area, which the resumed run must import. */
static NTSTATUS x87_native_call( void *opaque, ULONG number, ULONG arguments )
{
    struct fixture *f = opaque;
    I386_CONTEXT *ctx = f->context;
    XMM_SAVE_AREA32 fx;
    double two = 2.0;
    (void)arguments;
    assert( number == 0x1008 );
    assert( (ctx->FloatSave.ControlWord & 0xffff) == 0x27f );
    assert( ((ctx->FloatSave.StatusWord >> 11) & 7) == 6 );
    assert( (ctx->FloatSave.TagWord & 0xffff) == 0x0fff );
    assert( ld80_to_double( ctx->FloatSave.RegisterArea ) == 1.0 );
    assert( ld80_to_double( ctx->FloatSave.RegisterArea + 10 ) == M_PI );
    memcpy( &fx, ctx->ExtendedRegisters, sizeof(fx) );
    assert( fx.ControlWord == 0x27f && fx.TagWord == 0x03 );
    memcpy( &fx.FloatRegisters[0], &two, sizeof(two) );
    memcpy( ctx->ExtendedRegisters, &fx, sizeof(fx) );
    f->calls++;
    return 0;
}
static const struct wine_nx_wow64_host x87_host = {read_guest, x87_native_call, NULL, NULL};
static NTSTATUS native_call( void *opaque, ULONG number, ULONG arguments )
{
    struct fixture *f = opaque;
    ULONG value;
    XMM_SAVE_AREA32 fx;
    assert( number == 0x1007 );
    assert( !read_guest( f, arguments, &value, 4 ) && value == 42 );
    f->calls++;
    f->context->Ebx = 0xcafebabe;
    memcpy( &fx, f->context->ExtendedRegisters, sizeof(fx) );
    assert( fx.XmmRegisters[0].Low == 42 );
    fx.XmmRegisters[0].Low = 77;
    memcpy( f->context->ExtendedRegisters, &fx, sizeof(fx) );
    if (f->nested)
    {
        I386_CONTEXT nested;
        ULONGLONG executed;
        init_context( &nested, BASE + 0x100, BASE + 0x7000 );
        assert( !wine_nx_box64_run( &nested, BASE + 0x3000, &f->gates, &host, f,
                                   BASE + 0x8020, 100, &executed ) );
        assert( nested.Eax == 11 && COUNT_IS(executed, 3) );
    }
    return 100;
}

int main(void)
{
    /* Real i386 instructions: FS load, arithmetic, SSE, native call, resume,
     * guest store, completion. Gate bytes are INT3 traps, never executed. */
    static const unsigned char program[] = {
        0x64,0xa1,0,0,0,0,                 /* mov eax, fs:[0] */
        0x83,0xc0,35,                       /* add eax, 35 */
        0x66,0x0f,0x6e,0xc0,                /* movd xmm0, eax */
        0x50,                               /* push eax */
        0x68,0xef,0xbe,0xad,0xde,            /* caller return slot */
        0xb8,7,0x10,0,0,                    /* syscall number */
        0xba,0,0x80,0,0x10,                 /* syscall gate */
        0xff,0xd2,                           /* call edx */
        0x83,0xc4,8,                         /* pop caller slot + arg */
        0x83,0xc0,1,                         /* native result + 1 */
        0x66,0x0f,0x7e,0xc1,                /* movd ecx, xmm0 */
        0xa3,4,0x30,0,0x10,                 /* store eax */
        0xba,0x20,0x80,0,0x10,              /* completion gate */
        0xff,0xe2                            /* jmp edx */
    };
    static const unsigned char nested[] = {0xb8,11,0,0,0,0xba,0x20,0x80,0,0x10,0xff,0xe2};
    static const unsigned char unix_program[] = {
        0x68,0x40,0x30,0x20,0x10, /* args */
        0x6a,17,                  /* code */
        0x68,0x98,0xba,0xdc,0xfe, /* high handle word */
        0x68,0x10,0x32,0x54,0x76, /* low handle word */
        0xba,0x10,0x80,0,0x10,   /* Unix-call gate */
        0xff,0xd2,
        0xba,0x20,0x80,0,0x10,
        0xff,0xe2
    };
    unsigned char *memory;
    I386_CONTEXT context;
    struct fixture f = {&context, {BASE + 0x8000, BASE + 0x8010}, 0, TRUE};
    ULONGLONG executed;
    ULONG result;
    NTSTATUS status;
#ifdef __SWITCH__
    consoleInit( NULL );
#else
    struct sigaction action = {0}, previous_segv, previous_bus;
    sigemptyset( &action.sa_mask );
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = operand_fault_handler;
    assert( !sigaction( SIGSEGV, &action, &previous_segv ) );
    assert( !sigaction( SIGBUS, &action, &previous_bus ) );
#endif
#ifdef WINE_NX_BOX64_DYNAREC
    puts( "BUILD nx-dynarec-2 (host timer)" );
#endif
    memory = allocate_guest();
    if (memory != (void *)(uintptr_t)BASE)
    {
        puts( "Unable to map the fixed guest test arena; execution was not attempted." );
#ifdef __SWITCH__
        consoleUpdate( NULL );
        svcSleepThread( 5000000000ULL );
        consoleExit( NULL );
#endif
        return 1;
    }
    memset( memory, 0xcc, SIZE );
    memcpy( memory, program, sizeof(program) );
    memcpy( memory + 0x100, nested, sizeof(nested) );
    *(ULONG *)(memory + 0x3000) = 7;
    init_context( &context, BASE, BASE + 0x6000 );
    status = wine_nx_box64_run( &context, BASE + 0x3000, &f.gates, &host, &f,
                               BASE + 0x8020, 100, &executed );
    printf( "execution status=%08x eax=%u ecx=%u instructions=%llu\n", (unsigned)status,
            (unsigned)context.Eax, (unsigned)context.Ecx, (unsigned long long)executed );
    assert( status == STATUS_SUCCESS && f.calls == 1 );
    if (context.Eax != 101 || context.Ecx != 77 || context.Ebx != 0xcafebabe)
    {
        printf( "FAIL initial register result: expected eax=101 ecx=77 ebx=cafebabe; ebx=%08x\n",
                (unsigned)context.Ebx );
#ifdef __SWITCH__
        consoleUpdate( NULL );
#endif
    }
    assert( context.Eax == 101 && context.Ecx == 77 && context.Ebx == 0xcafebabe );
    assert( context.Eip == BASE + 0x8020 && context.Esp == BASE + 0x6000 );
    assert( !read_guest( &f, BASE + 0x3004, &result, 4 ) && result == 101 );
    assert( COUNT_IS(executed, 14) );

    memcpy( memory + 0x400, unix_program, sizeof(unix_program) );
    init_context( &context, BASE + 0x400, BASE + 0x6000 );
    assert( !wine_nx_box64_run( &context, BASE + 0x3000, &f.gates, &host, &f,
                               BASE + 0x8020, 100, &executed ) );
    assert( context.Eax == (ULONG)STATUS_INVALID_HANDLE && context.Edi == 0xbadc0de );
    assert( context.Esp == BASE + 0x6000 && f.calls == 2 && COUNT_IS(executed, 8) );

    /* DLL boundary: stop before the gate, dispatch in the caller, then resume.
     * EAX and the gate stack must survive the Unix-side return untouched. */
    {
        const struct wine_nx_wow64_host stopping_host = { read_guest, NULL, NULL, NULL };
        init_context( &context, BASE, BASE + 0x6000 );
        assert( !wine_nx_box64_run( &context, BASE + 0x3000, &f.gates, &stopping_host, &f,
                                   0, 100, &executed ) );
        assert( context.Eip == f.gates.syscall && context.Eax == 0x1007 && f.calls == 2 );
        assert( !wine_nx_wow64_dispatch_gate( &context, &f.gates, &host, &f ) );
        assert( f.calls == 3 );
        assert( !wine_nx_box64_run( &context, BASE + 0x3000, &f.gates, &stopping_host, &f,
                                   BASE + 0x8020, 100, &executed ) );
        assert( context.Eax == 101 && context.Ecx == 77 && context.Esp == BASE + 0x6000 );
    }

    /* WoW64 imports contexts captured by x86 RtlCaptureContext, whose 16-bit
     * selector stores leave stale upper bits in each 32-bit field (seen on
     * hardware as a refused resume at KiUserExceptionDispatcher). The same
     * program, with its FS load, syscall and resume, must run unchanged. */
    {
        struct fixture dirty = {&context, {BASE + 0x8000, BASE + 0x8010}, 0, FALSE};

        init_context( &context, BASE, BASE + 0x6000 );
        context.SegCs = 0xdead0023; context.SegSs = 0xbeef002b; context.SegDs = 0xcccc002b;
        context.SegEs = 0x1234002b; context.SegFs = 0xffff0053; context.SegGs = 0x8000002b;
        assert( !wine_nx_box64_run( &context, BASE + 0x3000, &dirty.gates, &host, &dirty,
                                   BASE + 0x8020, 100, &executed ) );
        assert( dirty.calls == 1 && context.Eax == 101 && context.Ecx == 77 && COUNT_IS(executed, 14) );
        assert( context.SegCs == 0x23 && context.SegSs == 0x2b && context.SegFs == 0x53 );
    }

#ifndef WINE_NX_BOX64_DYNAREC
    /* An actual infinite guest loop is stopped by the interpreter hook. */
    put_code( memory, 0x200, (const unsigned char[]){ 0xeb, 0xfe }, 2 );
    init_context( &context, BASE + 0x200, BASE + 0x6000 );
    assert( wine_nx_box64_run( &context, 0, &f.gates, &host, &f, 0, 10, &executed ) == STATUS_TIMEOUT );
    assert( COUNT_IS(executed, 10) );

#endif

    /* A Linux syscall must not escape through a native host syscall layer. */
    put_code( memory, 0x200, (const unsigned char[]){ 0xcd, 0x80 }, 2 );
    init_context( &context, BASE + 0x200, BASE + 0x6000 );
    assert( wine_nx_box64_run( &context, 0, &f.gates, &host, &f, 0, 10, &executed ) != STATUS_SUCCESS );
    /* x87 state crosses a gate both within one run and across the stop/free/
     * reimport boundary winebox64 uses: control word, stack, registers. */
    {
        static const unsigned char x87_program[] = {
            0xdb,0xe3,                          /* fninit */
            0xd9,0x2d,0x50,0x30,0,0x10,         /* fldcw [BASE+0x3050] */
            0xd9,0xeb,                          /* fldpi */
            0xd9,0xe8,                          /* fld1 */
            0x6a,0,                             /* push 0 (argument) */
            0x68,0xef,0xbe,0xad,0xde,           /* caller return slot */
            0xb8,0x08,0x10,0,0,                 /* syscall number */
            0xba,0,0x80,0,0x10,                 /* syscall gate */
            0xff,0xd2,                          /* call edx */
            0x83,0xc4,8,                        /* add esp, 8 */
            0xde,0xc1,                          /* faddp */
            0xdd,0x1d,0x58,0x30,0,0x10,         /* fstp qword [BASE+0x3058] */
            0xd9,0x3d,0x60,0x30,0,0x10,         /* fnstcw [BASE+0x3060] */
            0xba,0x20,0x80,0,0x10,0xff,0xe2     /* completion */
        };
        const struct wine_nx_wow64_host stopping_host = { read_guest, NULL, NULL, NULL };
        unsigned int pass;
        double sum;
        USHORT cw;

        memcpy( memory + 0x500, x87_program, sizeof(x87_program) );
        for (pass = 0; pass < 2; pass++)
        {
            *(USHORT *)(memory + 0x3050) = 0x27f;
            memset( memory + 0x3058, 0, 16 );
            init_context( &context, BASE + 0x500, BASE + 0x6000 );
            if (!pass)
                assert( !wine_nx_box64_run( &context, BASE + 0x3000, &f.gates, &x87_host, &f,
                                           BASE + 0x8020, 100, &executed ) );
            else
            {
                assert( !wine_nx_box64_run( &context, BASE + 0x3000, &f.gates, &stopping_host, &f,
                                           0, 100, &executed ) );
                assert( context.Eip == f.gates.syscall );
                assert( !wine_nx_wow64_dispatch_gate( &context, &f.gates, &x87_host, &f ) );
                assert( !wine_nx_box64_run( &context, BASE + 0x3000, &f.gates, &stopping_host, &f,
                                           BASE + 0x8020, 100, &executed ) );
            }
            memcpy( &sum, memory + 0x3058, sizeof(sum) );
            memcpy( &cw, memory + 0x3060, sizeof(cw) );
            assert( sum == M_PI + 2.0 && cw == 0x27f );
            assert( context.Eip == BASE + 0x8020 && context.Esp == BASE + 0x6000 );
        }
        assert( f.calls == 5 );
    }

    puts( "BEGIN CPUID/RDTSC" );
    /* CPUID reports the conservative feature set; RDTSC is monotonic. */
    {
        static const unsigned char cpuid_program[] = {
            0x31,0xc0,                          /* xor eax, eax */
            0x0f,0xa2,                          /* cpuid */
            0x89,0x1d,0x70,0x30,0,0x10,         /* mov [BASE+0x3070], ebx */
            0x89,0x15,0x74,0x30,0,0x10,         /* mov [BASE+0x3074], edx */
            0x89,0x0d,0x78,0x30,0,0x10,         /* mov [BASE+0x3078], ecx */
            0xb8,1,0,0,0,                       /* mov eax, 1 */
            0x0f,0xa2,                          /* cpuid */
            0x89,0x15,0x7c,0x30,0,0x10,         /* mov [BASE+0x307c], edx */
            0x0f,0x31,                          /* rdtsc */
            0xa3,0x80,0x30,0,0x10,              /* mov [BASE+0x3080], eax */
            0x89,0x15,0x84,0x30,0,0x10,         /* mov [BASE+0x3084], edx */
            0x0f,0x31,                          /* rdtsc */
            0xa3,0x88,0x30,0,0x10,              /* mov [BASE+0x3088], eax */
            0x89,0x15,0x8c,0x30,0,0x10,         /* mov [BASE+0x308c], edx */
            0xba,0x20,0x80,0,0x10,0xff,0xe2     /* completion */
        };
        extern ULONGLONG wine_nx_box64_tsc_reads;
        ULONGLONG reads_before = wine_nx_box64_tsc_reads;
        ULONGLONG first, second;
        ULONG features;

        memcpy( memory + 0x600, cpuid_program, sizeof(cpuid_program) );
        init_context( &context, BASE + 0x600, BASE + 0x6000 );
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &host, &f, BASE + 0x8020, 100, &executed ) );
        assert( !memcmp( memory + 0x3070, "GenuineIntel", 12 ) );
        memcpy( &features, memory + 0x307c, 4 );
        assert( (features & 0x07808111) == 0x07808111 ); /* FPU TSC CX8 CMOV MMX FXSR SSE SSE2 */
        memcpy( &first, memory + 0x3080, 8 );
        memcpy( &second, memory + 0x3088, 8 );
        assert( first && second >= first );
        assert( wine_nx_box64_tsc_reads == reads_before + 2 );
        puts( "PASS CPUID/RDTSC via host timer helper" );
    }

    /* MMX registers survive the stop/free/reimport boundary too. */
    {
        static const unsigned char mmx_program[] = {
            0xb8,0x78,0x56,0x34,0x12,           /* mov eax, 0x12345678 */
            0x0f,0x6e,0xc0,                     /* movd mm0, eax */
            0x6a,0,                             /* push 0 */
            0x68,0xef,0xbe,0xad,0xde,           /* caller return slot */
            0xb8,0x09,0x10,0,0,                 /* syscall number (not dispatched) */
            0xba,0,0x80,0,0x10,                 /* syscall gate */
            0xff,0xd2,                          /* call edx */
            0x83,0xc4,8,                        /* add esp, 8 */
            0x0f,0x7e,0xc1,                     /* movd ecx, mm0 */
            0x0f,0x77,                          /* emms */
            0x89,0x0d,0x90,0x30,0,0x10,         /* mov [BASE+0x3090], ecx */
            0xba,0x20,0x80,0,0x10,0xff,0xe2     /* completion */
        };
        const struct wine_nx_wow64_host stopping_host = { read_guest, NULL, NULL, NULL };
        ULONG value;

        memcpy( memory + 0x700, mmx_program, sizeof(mmx_program) );
        init_context( &context, BASE + 0x700, BASE + 0x6000 );
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &stopping_host, &f, 0, 100, &executed ) );
        assert( context.Eip == f.gates.syscall );
        /* Emulate the dispatcher's return: resume after the call, pop it. */
        assert( !read_guest( &f, context.Esp, &value, 4 ) );
        context.Eip = value;
        context.Esp += 4;
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &stopping_host, &f, BASE + 0x8020, 100, &executed ) );
        memcpy( &value, memory + 0x3090, 4 );
        assert( value == 0x12345678 );
    }
    printf( "x87 across gates and run boundaries, CPUID, RDTSC and MMX passed\n" );

    /* Instruction fetches use checked reads once per code page, not per byte:
     * on Horizon each checked read is two Wine exception frames. */
    {
        static const unsigned char loop_program[] = {
            0xb9,0x10,0x27,0,0,                 /* mov ecx, 10000 */
            0x49,                               /* dec ecx */
            0x75,0xfd,                          /* jnz dec */
            0xba,0x20,0x80,0,0x10,0xff,0xe2     /* completion */
        };
        memcpy( memory + 0x800, loop_program, sizeof(loop_program) );
        init_context( &context, BASE + 0x800, BASE + 0x6000 );
        guest_reads = 0;
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &host, &f, BASE + 0x8020, 100000, &executed ) );
        assert( context.Ecx == 0 && COUNT_IS(executed, 20003) );
        printf( "fetch cache: %llu instructions, %u checked reads\n", (unsigned long long)executed, guest_reads );
        assert( guest_reads <= 4 );
    }
#ifdef WINE_NX_BOX64_DYNAREC
    /* Blocks link directly: a loop calling into another block 5000 times does
     * not check the code's hash on each transition. */
    {
        static const unsigned char call_loop[] = {
            0x31,0xc0,                          /* xor eax, eax */
            0xb9,0x88,0x13,0,0,                 /* mov ecx, 5000 */
            0xe8,0x0a,0,0,0,                    /* call sub */
            0x49,                               /* dec ecx */
            0x75,0xf8,                          /* jnz call */
            0xba,0x20,0x80,0,0x10,0xff,0xe2,    /* completion */
            0x40,                               /* sub: inc eax */
            0xc3                                /* ret */
        };
        unsigned int tests_before;

        put_code( memory, 0x900, call_loop, sizeof(call_loop) );
        init_context( &context, BASE + 0x900, BASE + 0x6000 );
        tests_before = wine_nx_box64_block_tests;
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &host, &f, BASE + 0x8020, 100000, &executed ) );
        printf( "call loop: eax=%u, %u block validations\n", (unsigned)context.Eax,
                wine_nx_box64_block_tests - tests_before );
        assert( context.Eax == 5000 && context.Ecx == 0 );
        assert( wine_nx_box64_block_tests - tests_before < 50 );
    }
#endif
    /* Instruction-fetch failures are reported without dereferencing the PC. */
    init_context( &context, BASE - 1, BASE + 0x6000 );
    assert( wine_nx_box64_run( &context, 0, &f.gates, &host, &f, 0, 10, &executed ) == STATUS_ACCESS_VIOLATION );

    /* These faults occur in real interpreter dereferences, after the checked
     * instruction fetch. Recovering multiple faults also verifies the POSIX
     * signal mask is restored and the Box64 atomic mutex is never stranded. */
    int faults_before_operands = native_faults;
    assert( !protect_fault_page( FALSE ) );
    {
        static const unsigned char fault_programs[][7] = {
            {0xa1,0,0xf0,0,0x10},            /* mov eax, [protected] */
            {0xa3,0,0xf0,0,0x10},            /* mov [protected], eax */
            {0x87,0x05,0,0xf0,0,0x10},       /* xchg [protected], eax */
            {0xf0,0x01,0x05,0,0xf0,0,0x10},  /* lock add [protected], eax */
        };
        unsigned int i;
        assert( !wine_nx_box64_handle_fault( BASE + SIZE - 0x1000 ) );
        for (i = 0; i < sizeof(fault_programs) / sizeof(fault_programs[0]); ++i)
        {
            put_code( memory, 0x200, fault_programs[i], sizeof(fault_programs[i]) );
            init_context( &context, BASE + 0x200, BASE + 0x6000 );
            assert( wine_nx_box64_run( &context, 0, &f.gates, &host, &f, 0, 10,
                                       &executed ) == STATUS_ACCESS_VIOLATION );
            assert( COUNT_IS(executed, 1) && context.Eip == BASE + 0x200 );
        }
    }
    /* Valid opcode, inaccessible immediate: the raw decoder also unwinds. */
    memory[SIZE - 0x1001] = 0xb8;
    init_context( &context, BASE + SIZE - 0x1001, BASE + 0x6000 );
    assert( wine_nx_box64_run( &context, 0, &f.gates, &host, &f, 0, 10,
                               &executed ) == STATUS_ACCESS_VIOLATION );
    assert( COUNT_IS(executed, 1) && context.Eip == BASE + SIZE - 0x1001 );
    assert( native_faults == faults_before_operands + 5 && !protect_fault_page( TRUE ) );
    {
        static const unsigned char recovered[] = {
            0xb8,29,0,0,0,                 /* mov eax,29 */
            0x87,0x05,8,0x30,0,0x10,     /* xchg [BASE+0x3008],eax */
            0xba,0x20,0x80,0,0x10,0xff,0xe2
        };
        *(ULONG *)(memory + 0x3008) = 17;
        put_code( memory, 0x200, recovered, sizeof(recovered) );
        init_context( &context, BASE + 0x200, BASE + 0x6000 );
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &host, &f,
                                   BASE + 0x8020, 10, &executed ) );
        assert( context.Eax == 17 && *(ULONG *)(memory + 0x3008) == 29 );
        /* Reuse the same guest address with different code, reported: a stale
         * block would store 29 again. No executable-alias writes are permitted. */
        put_code( memory, 0x201, (const unsigned char[]){ 43 }, 1 );
        init_context( &context, BASE + 0x200, BASE + 0x6000 );
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &host, &f,
                                   BASE + 0x8020, 10, &executed ) );
        assert( context.Eax == 29 && *(ULONG *)(memory + 0x3008) == 43 );
#ifdef WINE_NX_BOX64_DYNAREC
        /* Freed code loses its blocks: new code at the address is translated afresh. */
        wine_nx_box64_invalidate( BASE + 0x200, sizeof(recovered), 1 );
        memory[0x201] = 55;
        init_context( &context, BASE + 0x200, BASE + 0x6000 );
        assert( !wine_nx_box64_run( &context, 0, &f.gates, &host, &f,
                                   BASE + 0x8020, 10, &executed ) );
        assert( context.Eax == 43 && *(ULONG *)(memory + 0x3008) == 55 );
#endif
    }
    printf( "Native operand/decoder fault recovery: %d faults, subsequent atomic execution passed\n",
            native_faults );
    assert( release_guest( memory ) == 0 );
#ifndef __SWITCH__
    assert( !sigaction( SIGSEGV, &previous_segv, NULL ) );
    assert( !sigaction( SIGBUS, &previous_bus, NULL ) );
#endif
#ifdef WINE_NX_BOX64_DYNAREC
    extern uint64_t wine_nx_box64_dynarec_bytes;
    extern unsigned long long wine_nx_box64_native_entries;
    assert( wine_nx_box64_dynarec_bytes > 0 && wine_nx_box64_native_entries > 0 );
    printf( "Native dispatch entries: %llu\n", wine_nx_box64_native_entries );
    printf( "Dynarec emitted bytes: %llu\n", (unsigned long long)wine_nx_box64_dynarec_bytes );
    puts( "Box64 i386 dynarec: gates, FS, SSE, x87, reentry, direct block links, reported code changes and fault "
          "recovery passed" );
    puts( "Instruction budgets and precise fault contexts are not validated by this dynarec test." );
#else
    puts( "Box64 i386 execution: native gate round trip, FS, SSE, reentry and bounded execution passed" );
#endif
#ifdef __SWITCH__
    consoleUpdate( NULL );
    svcSleepThread( 5000000000ULL );
    consoleExit( NULL );
#endif
    return 0;
}
