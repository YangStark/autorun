/*
 * Horizon i386 context transfers.
 *
 * Copyright 2026 Wine-NX contributors
 * Based on the ARM64 context transfer code in signal_arm64.c.
 * This library is free software under LGPL-2.1-or-later.
 */
#ifndef __WINE_HORIZON_WOW64_H
#define __WINE_HORIZON_WOW64_H

#include <string.h>

/* Callers hold a stopped/current CPU context. Do not copy unrequested groups:
 * WoW64 commonly changes only CONTROL during callbacks and NtContinue. */
static inline NTSTATUS horizon_transfer_i386_context( WOW64_CPURESERVED *cpu,
                                                       I386_CONTEXT *saved,
                                                       I386_CONTEXT *context, BOOL set )
{
    I386_CONTEXT *dst = set ? saved : context;
    const I386_CONTEXT *src = set ? context : saved;
    DWORD flags;

    if (!cpu || !saved || !context || cpu->Machine != IMAGE_FILE_MACHINE_I386)
        return STATUS_INVALID_PARAMETER;
    flags = context->ContextFlags & ~CONTEXT_i386;
    /* No XSAVE buffer/feature negotiation is implemented by this bridge. */
    if (flags & (CONTEXT_I386_XSTATE & ~CONTEXT_i386)) return STATUS_NOT_SUPPORTED;

    if (flags & CONTEXT_I386_INTEGER)
    {
        dst->Eax = src->Eax; dst->Ebx = src->Ebx;
        dst->Ecx = src->Ecx; dst->Edx = src->Edx;
        dst->Esi = src->Esi; dst->Edi = src->Edi;
    }
    /* Selectors are 16 bits. x86 RtlCaptureContext stores them with 16-bit moves
     * into an uninitialized CONTEXT, so the upper half of each field is stale. */
    if (flags & CONTEXT_I386_CONTROL)
    {
        dst->Esp = src->Esp; dst->Ebp = src->Ebp; dst->Eip = src->Eip;
        dst->EFlags = src->EFlags; dst->SegCs = (WORD)src->SegCs; dst->SegSs = (WORD)src->SegSs;
        if (set) cpu->Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
    }
    if (flags & CONTEXT_I386_SEGMENTS)
    {
        dst->SegDs = (WORD)src->SegDs; dst->SegEs = (WORD)src->SegEs;
        dst->SegFs = (WORD)src->SegFs; dst->SegGs = (WORD)src->SegGs;
    }
    if (flags & CONTEXT_I386_DEBUG_REGISTERS)
    {
        dst->Dr0 = src->Dr0; dst->Dr1 = src->Dr1;
        dst->Dr2 = src->Dr2; dst->Dr3 = src->Dr3;
        dst->Dr6 = src->Dr6; dst->Dr7 = src->Dr7;
    }
    if (flags & CONTEXT_I386_FLOATING_POINT) dst->FloatSave = src->FloatSave;
    if (flags & CONTEXT_I386_EXTENDED_REGISTERS)
        memcpy( dst->ExtendedRegisters, src->ExtendedRegisters, sizeof(dst->ExtendedRegisters) );
    return STATUS_SUCCESS;
}

#endif
