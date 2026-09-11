/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include <stdio.h>
#include "wow64_box64_engine.h"
#include "../../dlls/winebox64/unixlib.h"
#include "wine/unixlib.h"
#include "rtlsupportapi.h"
static NTSTATUS read_guest( void *opaque, ULONG address, void *buffer, SIZE_T size )
{
    SIZE_T read = 0;
    NTSTATUS status;
    if (size > (ULONGLONG)0x100000000 - address) return STATUS_ACCESS_VIOLATION;
    status = NtReadVirtualMemory( NtCurrentProcess(), ULongToPtr(address), buffer, size, &read );
    return status ? status : read == size ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
}
static NTSTATUS run_guest( void *args )
{
    static const struct wine_nx_wow64_host host = { read_guest, NULL, NULL, NULL };
    struct winebox64_run_params *p = args;
    if (!p || p->version != WINEBOX64_ABI_VERSION || p->size != sizeof(*p))
        return STATUS_INVALID_PARAMETER;
    p->executed = 0;
    /* Use the existing run entry for the handshake: indexing a new entry in
     * an older table would dereference beyond that table before version checks. */
    if (p->operation == winebox64_query_abi) return STATUS_SUCCESS;
    if (p->operation != winebox64_execute) return STATUS_INVALID_PARAMETER;
    {
        NTSTATUS status = wine_nx_box64_run( p->context, p->fs_base, &p->gates, &host, NULL,
                                            0, p->budget, &p->executed );
#ifdef __SWITCH__
        extern void wine_nx_runtime_trace( const char * );
        char message[160];
        if (status && status != STATUS_TIMEOUT)
        {
            snprintf( message, sizeof(message), "[BOX64] status=%08x EIP=%08x EAX=%08x instructions=%llu",
                      (unsigned)status, p->context ? (unsigned)p->context->Eip : 0,
                      p->context ? (unsigned)p->context->Eax : 0, (unsigned long long)p->executed );
            wine_nx_runtime_trace( message );
        }
#endif
        return status;
    }
}
extern NTSTATUS wine_nx_call_ntdll_wow64( unixlib_handle_t handle, ULONG code, ULONG args );
static NTSTATUS call_guest_unix( void *args )
{
    struct winebox64_unix_params *p = args;
    if (!p || p->version != WINEBOX64_ABI_VERSION || p->size != sizeof(*p))
        return STATUS_INVALID_PARAMETER;
    return wine_nx_call_ntdll_wow64( p->handle, p->code, p->arguments );
}
const unixlib_entry_t wine_nx_winebox64_unix_funcs[] = { run_guest, call_guest_unix };
