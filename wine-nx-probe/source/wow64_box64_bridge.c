/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include "wow64_box64_bridge.h"

NTSTATUS wine_nx_wow64_dispatch_gate( I386_CONTEXT *context,
                                     const struct wine_nx_wow64_gates *gates,
                                     const struct wine_nx_wow64_host *host, void *opaque )
{
    ULONG stack[5], esp, number, arguments;
    NTSTATUS status;
    BOOL unix_call;

    if (!context || !gates || !host || !host->read || !gates->syscall ||
        !gates->unix_call || gates->syscall == gates->unix_call)
        return STATUS_INVALID_PARAMETER;
    if (context->Eip == gates->syscall) unix_call = FALSE;
    else if (context->Eip == gates->unix_call) unix_call = TRUE;
    else return STATUS_ILLEGAL_INSTRUCTION;
    if ((unix_call && !host->unix_call) || (!unix_call && !host->syscall))
        return STATUS_NOT_SUPPORTED;

    esp = context->Esp;
    /* Reject wraparound before reading or changing the guest state. */
    if (esp > 0xffffffffu - (unix_call ? 20u : 8u)) return STATUS_ACCESS_VIOLATION;
    status = host->read( opaque, esp, stack, unix_call ? sizeof(stack) : sizeof(stack[0]) );
    if (status) return status;

    number = context->Eax;
    arguments = esp + 8;
    /* Match wow64cpu's syscall_32to64 and unix_call_32to64 stack layouts.
     * Publish the continuation before entering Wine: callbacks/NtContinue
     * observe and can replace this context while the native call is active. */
    context->Eip = stack[0];
    context->Esp = esp + (unix_call ? 20 : 4);
    if (unix_call)
        status = host->unix_call( opaque, (ULONGLONG)stack[1] | ((ULONGLONG)stack[2] << 32),
                                 stack[3], stack[4] );
    else status = host->syscall( opaque, number, arguments );
    /* wow64cpu returns through the full saved context in that case: RtlUnwind
     * and C++ catch continuations resume via NtContinue with a meaningful Eax. */
    if (host->context_replaced && host->context_replaced( opaque )) return STATUS_SUCCESS;
    context->Eax = status;
    return STATUS_SUCCESS;
}
