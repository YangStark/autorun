/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINEBOX64_UNIXLIB_H
#define WINEBOX64_UNIXLIB_H
#include "../../wine-nx-probe/source/wow64_box64_bridge.h"
#define WINEBOX64_ABI_VERSION 4

enum winebox64_run_operation { winebox64_query_abi, winebox64_execute };

struct winebox64_run_params
{
    ULONG version;
    ULONG size;
    ULONG operation;
    ULONG fs_base;
    I386_CONTEXT *context;
    struct wine_nx_wow64_gates gates;
    ULONGLONG budget;
    ULONGLONG executed;
    /* An access violation the run stopped at: the address, and 0 read,
     * 1 write or 8 execute, for the exception raised into the guest. */
    ULONG fault_address;
    ULONG fault_access;
};
struct winebox64_unix_params
{
    ULONG version;
    ULONG size;
    ULONGLONG handle;
    ULONG code;
    ULONG arguments;
};
/* Guest memory whose translated code is no longer valid: freed or unmapped
 * (destroy), or re-protected or flushed. */
struct winebox64_invalidate_params
{
    ULONG version;
    ULONG size;
    ULONGLONG address;
    ULONGLONG length;
    ULONG destroy;
    ULONG pad;
};
enum winebox64_calls { winebox64_run, winebox64_call_unix, winebox64_invalidate };

/* Both sides are native ARM64. Keep the version first so an older native
 * table can reject a new layout before accessing any changed fields. */
C_ASSERT( offsetof(struct winebox64_run_params, context) == 16 );
C_ASSERT( sizeof(struct winebox64_run_params) == 56 );
C_ASSERT( sizeof(struct winebox64_unix_params) == 24 );
C_ASSERT( sizeof(struct winebox64_invalidate_params) == 32 );
#endif
