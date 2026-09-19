/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "wow64_box64_engine.h"
#include "../../dlls/winebox64/unixlib.h"
#include "wine/unixlib.h"

extern const unixlib_entry_t wine_nx_winebox64_unix_funcs[];

static unsigned int run_calls, unix_calls, read_calls;
static BOOL partial_read;
static NTSTATUS read_status;
static I386_CONTEXT context;
static NTSTATUS run_status = STATUS_TIMEOUT;
TEB *WINAPI NtCurrentTeb(void)
{
    static TEB teb;
    return &teb;
}
#ifdef __SWITCH__
int wine_nx_runtime_verbose;
static unsigned int trace_calls, error_calls, fault_calls, fault_registers;
static unsigned int fault_modules, exit_calls;
extern void wine_nx_box64_trace_exit( const I386_CONTEXT * );
int wine_nx_image_at( const void *address, void **base, char *name, size_t size )
{
    if (address != ULongToPtr(0x42424242)) return 0;
    *base = ULongToPtr(0x42420000);
    snprintf( name, size, "fixture.dll" );
    return 1;
}
void wine_nx_runtime_trace( const char *message )
{
    if (!strncmp( message, "[BOX64RUN]", 10 )) ++trace_calls;
    else if (!strncmp( message, "[BOX64] status=", 15 )) ++error_calls;
    else if (!strncmp( message, "[BOX64FAULT]", 12 ))
    {
        ++fault_calls;
        if (strstr( message, "eip=" )) ++fault_registers;
        if (strstr( message, "fixture.dll+4242" ))
        {
            assert( strstr( message, "preceding=90909090909090909090909090909090" ) );
            ++fault_modules;
        }
    }
    else if (!strncmp( message, "[BOX64EXIT]", 11 )) ++exit_calls;
    else assert( 0 );
}
#endif

NTSTATUS WINAPI NtReadVirtualMemory( HANDLE process, const void *address, void *buffer,
                                     SIZE_T size, SIZE_T *read )
{
    assert( process == NtCurrentProcess() );
    ++read_calls;
    if (address == ULongToPtr(0x42424232) && size == 16)
    {
        memset( buffer, 0x90, size );
        *read = size;
        return STATUS_SUCCESS;
    }
    if (address != ULongToPtr(0x10000000)) return STATUS_ACCESS_VIOLATION;
    if (read_status) return read_status;
    memset( buffer, 0x42, size );
    *read = partial_read ? size - 1 : size;
    return STATUS_SUCCESS;
}

NTSTATUS wine_nx_box64_run( I386_CONTEXT *ctx, ULONG fs_base,
                          const struct wine_nx_wow64_gates *gates,
                          const struct wine_nx_wow64_host *host, void *opaque,
                          ULONG completion, ULONGLONG budget, ULONGLONG *executed )
{
    unsigned char buffer[4];
    assert( ctx == &context && fs_base == 0x10002000 );
    assert( gates->syscall == 0x10001000 && gates->unix_call == 0x10001010 );
    assert( host->read && !host->syscall && host->unix_call && host->context_replaced && !opaque );
    assert( !completion && budget == 123 );
    ++run_calls;
    if (run_calls > 1) return run_status;
    /* Guest reads cannot wrap into native address space or accept short copies. */
    assert( host->read( opaque, 0xffffffff, buffer, sizeof(buffer) ) == STATUS_ACCESS_VIOLATION );
    assert( !read_calls );
    assert( host->read( opaque, 0x10000000, buffer, sizeof(buffer) ) == STATUS_SUCCESS );
    assert( buffer[0] == 0x42 && buffer[3] == 0x42 );
    partial_read = TRUE;
    assert( host->read( opaque, 0x10000000, buffer, sizeof(buffer) ) == STATUS_PARTIAL_COPY );
    partial_read = FALSE;
    read_status = STATUS_ACCESS_VIOLATION;
    assert( host->read( opaque, 0x10000000, buffer, sizeof(buffer) ) == STATUS_ACCESS_VIOLATION );
    *executed = 7;
    return STATUS_TIMEOUT;
}

/* What the engine says the access violation was about. */
void wine_nx_box64_last_fault( ULONG *address, ULONG *access )
{
    *address = 0x1234;
    *access = 1;
}

static unsigned int invalidations;
static uintptr_t invalidated_address;
static size_t invalidated_size;
static int invalidated_destroy;
void wine_nx_box64_invalidate( uintptr_t address, size_t size, int destroy )
{
    ++invalidations;
    invalidated_address = address;
    invalidated_size = size;
    invalidated_destroy = destroy;
}

NTSTATUS wine_nx_call_ntdll_wow64( unixlib_handle_t handle, ULONG code, ULONG arguments )
{
    assert( handle == (ULONGLONG)0xfedcba9876543210 );
    assert( code == 17 && arguments == 0x87654321 );
    ++unix_calls;
    return STATUS_INVALID_HANDLE;
}

int main(void)
{
    unixlib_entry_t run = wine_nx_winebox64_unix_funcs[winebox64_run];
    unixlib_entry_t call_unix = wine_nx_winebox64_unix_funcs[winebox64_call_unix];
    struct winebox64_run_params p = {0};
    struct winebox64_unix_params u = { WINEBOX64_ABI_VERSION, sizeof(u),
                                     (ULONGLONG)0xfedcba9876543210, 17, 0x87654321 };

    assert( run( NULL ) == STATUS_INVALID_PARAMETER );
    p.version = WINEBOX64_ABI_VERSION - 1;
    p.size = sizeof(p);
    p.executed = 999;
    assert( run( &p ) == STATUS_INVALID_PARAMETER && p.executed == 999 );
    p.version = WINEBOX64_ABI_VERSION;
    --p.size;
    assert( run( &p ) == STATUS_INVALID_PARAMETER && p.executed == 999 );
    p.size = sizeof(p);
    p.operation = winebox64_query_abi;
    assert( run( &p ) == STATUS_SUCCESS && !p.executed && !run_calls );
    p.operation = 99;
    assert( run( &p ) == STATUS_INVALID_PARAMETER && !run_calls );
    p.operation = winebox64_execute;
    p.context = &context;
    p.fs_base = 0x10002000;
    p.gates.syscall = 0x10001000;
    p.gates.unix_call = 0x10001010;
    p.budget = 123;
    assert( run( &p ) == STATUS_TIMEOUT && p.executed == 7 && run_calls == 1 );
#ifdef __SWITCH__
    /* Quiet execution must not read instructions just to format diagnostics,
     * nor consume the bounded verbose trace allowance. */
    assert( !trace_calls && !error_calls );
    context.Eip = 0x10000000;
    read_status = STATUS_SUCCESS;
    read_calls = 0;
    for (unsigned int i = 0; i < 800; ++i) assert( run( &p ) == STATUS_TIMEOUT );
    assert( !read_calls && !trace_calls );
    wine_nx_runtime_verbose = 1;
    assert( run( &p ) == STATUS_TIMEOUT );
    assert( read_calls == 1 && trace_calls == 2 );
    wine_nx_runtime_verbose = 0;
    assert( run( &p ) == STATUS_TIMEOUT );
    assert( read_calls == 1 && trace_calls == 2 );
    wine_nx_runtime_verbose = 1;
    for (unsigned int i = 0; i < 800; ++i) assert( run( &p ) == STATUS_TIMEOUT );
    assert( read_calls == 768 && trace_calls == 1536 );
    wine_nx_runtime_verbose = 0;
    assert( !p.fault_address && !p.fault_access );
    run_status = STATUS_ACCESS_VIOLATION;
    assert( run( &p ) == STATUS_ACCESS_VIOLATION );
    /* The access violation's address and kind reach BTCpuSimulate, which raises it. */
    assert( p.fault_address == 0x1234 && p.fault_access == 1 );
    assert( error_calls == 1 && trace_calls == 1536 && read_calls == 769 );
    assert( fault_calls == 2 && fault_registers == 1 );
    /* A readable stack row followed by an inaccessible row stops safely. */
    context.Esp = 0x10000000;
    assert( run( &p ) == STATUS_ACCESS_VIOLATION );
    assert( read_calls == 775 && fault_calls == 9 && fault_registers == 2 && fault_modules == 4 );
    /* A stack at the address-space end never wraps a diagnostic read. */
    context.Esp = context.Ebp = 0xfffffffc;
    assert( run( &p ) == STATUS_ACCESS_VIOLATION );
    assert( read_calls == 775 && fault_registers == 3 );
    context.Ebp = 0;
    for (unsigned int i = 0; i < 10; ++i) assert( run( &p ) == STATUS_ACCESS_VIOLATION );
    assert( fault_registers == 4 && read_calls == 775 );
    /* Exit tracing has a separate budget, including after fault reports. */
    wine_nx_box64_trace_exit( NULL );
    assert( !exit_calls );
    context.Esp = 0x10000000;
    wine_nx_box64_trace_exit( &context );
    assert( exit_calls == 7 );
    unsigned int exit_reads = read_calls;
    wine_nx_box64_trace_exit( &context );
    assert( exit_calls == 7 && read_calls == exit_reads );
    puts( "WoW64 tracing: quiet execution, verbose cap, bounded fault stacks and address wrap checks passed" );
#endif

    assert( call_unix( NULL ) == STATUS_INVALID_PARAMETER );
    --u.version;
    assert( call_unix( &u ) == STATUS_INVALID_PARAMETER && !unix_calls );
    ++u.version;
    --u.size;
    assert( call_unix( &u ) == STATUS_INVALID_PARAMETER && !unix_calls );
    ++u.size;
    assert( call_unix( &u ) == STATUS_INVALID_HANDLE && unix_calls == 1 );
    {
        unixlib_entry_t invalidate = wine_nx_winebox64_unix_funcs[winebox64_invalidate];
        struct winebox64_invalidate_params v = { WINEBOX64_ABI_VERSION, sizeof(v), 0x10001000, 0x2000, 1, 0 };

        assert( invalidate( NULL ) == STATUS_INVALID_PARAMETER );
        --v.version;
        assert( invalidate( &v ) == STATUS_INVALID_PARAMETER && !invalidations );
        ++v.version;
        --v.size;
        assert( invalidate( &v ) == STATUS_INVALID_PARAMETER && !invalidations );
        ++v.size;
        v.address = 0x100000000ull;
        assert( invalidate( &v ) == STATUS_INVALID_PARAMETER && !invalidations );
        v.address = 0x10001000;
        assert( invalidate( &v ) == STATUS_SUCCESS && invalidations == 1 );
        assert( invalidated_address == 0x10001000 && invalidated_size == 0x2000 && invalidated_destroy == 1 );
        v.destroy = 0;
        assert( invalidate( &v ) == STATUS_SUCCESS && invalidations == 2 && !invalidated_destroy );
    }
    puts( "WoW64 native ABI: handshake, version/size, guest read limits, dispatch and code invalidation passed" );
    return 0;
}
