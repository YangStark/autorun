/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_NX_WOW64_BOX64_BRIDGE_H
#define WINE_NX_WOW64_BOX64_BRIDGE_H

#include <stdarg.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

/* Native C callbacks, not PE entry points. The PE adapter must use WINAPI when
 * calling Wow64SystemServiceEx and the PE Unix dispatcher, preserving x18/TEB.
 * Read must report guest faults instead of dereferencing unchecked addresses.
 * Unix-call handles must resolve to registered WoW64 tables, never arbitrary
 * guest-supplied native function pointers. */
struct wine_nx_wow64_host
{
    NTSTATUS (*read)( void *opaque, ULONG address, void *buffer, SIZE_T size );
    NTSTATUS (*syscall)( void *opaque, ULONG number, ULONG arguments );
    NTSTATUS (*unix_call)( void *opaque, ULONGLONG handle, ULONG code, ULONG arguments );
    /* Optional: TRUE (and consumed) when the native call replaced the whole
     * context, i.e. WoW64 set WOW64_CPURESERVED_FLAG_RESET_STATE for NtContinue,
     * exception or APC dispatch. The call's result still goes in Eax, as
     * wow64cpu puts it there; NtContinue's result is the new context's Eax. */
    BOOL (*context_replaced)( void *opaque );
};

struct wine_nx_wow64_gates
{
    ULONG syscall;
    ULONG unix_call;
};

/* Called only after Box64 stops BEFORE executing one of the registered gates
 * and exports the complete guest state into Wine's current I386_CONTEXT.
 * STATUS_SUCCESS means a gate was dispatched; its result lives in Eax, also
 * when the call replaced the context (see context_replaced).
 * Errors leave the context unchanged. Native callbacks may alter this same
 * context or reenter guest execution: no cached snapshot may overwrite it. */
NTSTATUS wine_nx_wow64_dispatch_gate( I386_CONTEXT *context,
                                     const struct wine_nx_wow64_gates *gates,
                                     const struct wine_nx_wow64_host *host, void *opaque );

#endif
