/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINEBOX64_UNIXLIB_H
#define WINEBOX64_UNIXLIB_H
#include "../../wine-nx-probe/source/wow64_box64_bridge.h"
#define WINEBOX64_ABI_VERSION 2

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
};
struct winebox64_unix_params
{
    ULONG version;
    ULONG size;
    ULONGLONG handle;
    ULONG code;
    ULONG arguments;
};
enum winebox64_calls { winebox64_run, winebox64_call_unix };

/* Both sides are native ARM64. Keep the version first so an older native
 * table can reject a new layout before accessing any changed fields. */
C_ASSERT( offsetof(struct winebox64_run_params, context) == 16 );
C_ASSERT( sizeof(struct winebox64_run_params) == 48 );
C_ASSERT( sizeof(struct winebox64_unix_params) == 24 );
#endif
