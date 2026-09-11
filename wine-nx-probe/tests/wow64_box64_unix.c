/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include <assert.h>
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

NTSTATUS WINAPI NtReadVirtualMemory( HANDLE process, const void *address, void *buffer,
                                     SIZE_T size, SIZE_T *read )
{
    assert( process == NtCurrentProcess() );
    assert( address == ULongToPtr(0x10000000) );
    ++read_calls;
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
    assert( host->read && !host->syscall && !host->unix_call && !opaque );
    assert( !completion && budget == 123 );
    ++run_calls;
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

    assert( call_unix( NULL ) == STATUS_INVALID_PARAMETER );
    --u.version;
    assert( call_unix( &u ) == STATUS_INVALID_PARAMETER && !unix_calls );
    ++u.version;
    --u.size;
    assert( call_unix( &u ) == STATUS_INVALID_PARAMETER && !unix_calls );
    ++u.size;
    assert( call_unix( &u ) == STATUS_INVALID_HANDLE && unix_calls == 1 );
    puts( "WoW64 native ABI: handshake, version/size, guest read limits and dispatch passed" );
    return 0;
}
