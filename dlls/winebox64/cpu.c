/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include "unixlib.h"
#include "cpuid.h"
#include "wine/unixlib.h"
#include "rtlsupportapi.h"

extern NTSTATUS WINAPI Wow64SystemServiceEx( UINT number, UINT *args );
static struct wine_nx_wow64_gates gates;

static void DECLSPEC_NORETURN fail( NTSTATUS status )
{
    /* Wine ignores ProcessInit's return value. Never continue with missing gates. */
    NtTerminateProcess( NtCurrentProcess(), status );
    for (;;) RtlRaiseStatus( status );
}

BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH) LdrDisableThreadCalloutsForDll( instance );
    return TRUE;
}

NTSTATUS WINAPI BTCpuProcessInit(void)
{
    struct winebox64_run_params params = {0};
    void *page = NULL;
    SIZE_T size = 0x1000;
    NTSTATUS status;
    if (gates.syscall) return STATUS_SUCCESS;
    if ((status = __wine_init_unix_call())) fail( status );
    params.version = WINEBOX64_ABI_VERSION;
    params.size = sizeof(params);
    params.operation = winebox64_query_abi;
    if ((status = WINE_UNIX_CALL( winebox64_run, &params ))) fail( status );
    /* Interpreter sentinel addresses, never executed by the host CPU. */
    status = NtAllocateVirtualMemory( NtCurrentProcess(), &page, 0x7fffffff, &size,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    if (status) fail( status );
    if (!page || (ULONG_PTR)page > 0x7fffffff ||
        size < 32 || size > (ULONGLONG)0x80000000 - (ULONG_PTR)page)
        fail( STATUS_INVALID_ADDRESS );
    memset( page, 0xcc, size );
    gates.syscall = PtrToUlong( page );
    gates.unix_call = gates.syscall + 16;
    return STATUS_SUCCESS;
}

void * WINAPI BTCpuGetBopCode(void) { return ULongToPtr( gates.syscall ); }
void * WINAPI __wine_get_unix_opcode(void) { return ULongToPtr( gates.unix_call ); }

static I386_CONTEXT *get_thread_context( ULONG *fs_base )
{
    TEB *teb = NtCurrentTeb();
    WOW64_CPURESERVED *cpu = teb->TlsSlots[WOW64_TLS_CPURESERVED];
    WOW64_CPU_AREA_INFO info;
    ULONG_PTR teb32 = (ULONG_PTR)teb + teb->WowTebOffset;
    NTSTATUS status;
    if (!cpu || cpu->Machine != IMAGE_FILE_MACHINE_I386 || !teb->WowTebOffset ||
        teb32 > (ULONGLONG)0x100000000 - sizeof(TEB32)) fail( STATUS_INVALID_PARAMETER );
    if ((status = RtlWow64GetCpuAreaInfo( cpu, 0, &info ))) fail( status );
    if (!info.Context || info.Machine != IMAGE_FILE_MACHINE_I386) fail( STATUS_INVALID_PARAMETER );
    *fs_base = teb32;
    return info.Context;
}

void WINAPI BTCpuThreadInit(void)
{
    ULONG fs_base;
    /* Every run owns its interpreter state; the Wine CPU area is authoritative.
     * Validate its existence now, without overwriting Wine's initial context. */
    get_thread_context( &fs_base );
}
/* Matches the adapter's CPUID (FPU TSC CX8 CMOV MMX FXSR SSE SSE2). */
BOOLEAN WINAPI BTCpuIsProcessorFeaturePresent( UINT feature )
{
    return winebox64_x86_feature_present( feature );
}
/* WoW64 asks the host for SystemEmulationProcessorInformation, which on ARM64
 * describes ARM; 32-bit programs (GetSystemInfo) must see the emulated x86 CPU. */
void WINAPI BTCpuUpdateProcessorInformation( SYSTEM_CPU_INFORMATION *info )
{
    winebox64_x86_processor_information( info );
}
NTSTATUS WINAPI BTCpuTurboThunkControl( ULONG enable )
{
    return enable ? STATUS_NOT_SUPPORTED : STATUS_SUCCESS;
}
NTSTATUS WINAPI BTCpuSuspendLocalThread( HANDLE thread, ULONG *count )
{
    /* The Horizon server suspends only threads still held at their start gate
     * (CREATE_SUSPENDED), which have run no guest code. Stopping a running
     * thread needs an interpreter-safe point; the server refuses it with
     * STATUS_NOT_SUPPORTED. */
    return NtSuspendThread( thread, count );
}
NTSTATUS WINAPI BTCpuGetContext( HANDLE thread, HANDLE process, void *unknown, I386_CONTEXT *ctx )
{
    return RtlWow64GetThreadContext( thread, ctx );
}
NTSTATUS WINAPI BTCpuSetContext( HANDLE thread, HANDLE process, void *unknown, I386_CONTEXT *ctx )
{
    return RtlWow64SetThreadContext( thread, ctx );
}
NTSTATUS WINAPI BTCpuResetToConsistentState( EXCEPTION_POINTERS *ptrs )
{
    /* Native operand fault recovery is not implemented yet. Do not report success. */
    return STATUS_NOT_SUPPORTED;
}
static NTSTATUS read_guest( void *opaque, ULONG address, void *buffer, SIZE_T size )
{
    SIZE_T read = 0;
    NTSTATUS status;
    if (size > (ULONGLONG)0x100000000 - address) return STATUS_ACCESS_VIOLATION;
    status = NtReadVirtualMemory( NtCurrentProcess(), ULongToPtr(address), buffer, size, &read );
    return status ? status : read == size ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
}
static NTSTATUS syscall_guest( void *opaque, ULONG number, ULONG arguments )
{
    return Wow64SystemServiceEx( number, ULongToPtr(arguments) );
}
static NTSTATUS unix_guest( void *opaque, ULONGLONG handle, ULONG code, ULONG arguments )
{
    struct winebox64_unix_params params = { WINEBOX64_ABI_VERSION, sizeof(params),
                                           handle, code, arguments };
    return WINE_UNIX_CALL( winebox64_call_unix, &params );
}
static BOOL context_replaced( void *opaque )
{
    WOW64_CPURESERVED *cpu = NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED];
    BOOL replaced = !!(cpu->Flags & WOW64_CPURESERVED_FLAG_RESET_STATE);

    cpu->Flags &= ~WOW64_CPURESERVED_FLAG_RESET_STATE;
    return replaced;
}
void WINAPI BTCpuSimulate(void)
{
    static const struct wine_nx_wow64_host host = { read_guest, syscall_guest, unix_guest, context_replaced };
    WOW64_CPURESERVED *cpu = NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED];
    struct winebox64_run_params params;
    NTSTATUS status;
    memset( &params, 0, sizeof(params) );
    params.version = WINEBOX64_ABI_VERSION;
    params.size = sizeof(params);
    params.operation = winebox64_execute;
    params.context = get_thread_context( &params.fs_base );
    params.gates = gates;
    params.budget = 1000000;
    for (;;)
    {
        cpu->Flags &= ~WOW64_CPURESERVED_FLAG_RESET_STATE;
        status = WINE_UNIX_CALL( winebox64_run, &params );
        if (status == STATUS_TIMEOUT) continue;
        if (status) fail( status );
        status = wine_nx_wow64_dispatch_gate( params.context, &gates, &host, NULL );
        if (status) fail( status );
    }
}
