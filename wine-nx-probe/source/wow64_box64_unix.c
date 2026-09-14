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
extern NTSTATUS wine_nx_call_ntdll_wow64( unixlib_handle_t handle, ULONG code, ULONG args );

/* The x86 unix call gate, taken without leaving the run. BTCpuSimulate's own
 * dispatch returned to PE code only to make another unix call to
 * call_guest_unix, then a third back into run_guest to go on: about 180,000
 * times a second from Direct3D's drawing thread in NFSU2, once per OpenGL call. */
static NTSTATUS unix_guest( void *opaque, ULONGLONG handle, ULONG code, ULONG arguments )
{
    return wine_nx_call_ntdll_wow64( (unixlib_handle_t)handle, code, arguments );
}

/* As BTCpuSimulate's: WoW64 replaced the whole context (NtContinue, an
 * exception or APC) while the call ran. */
static BOOL context_replaced( void *opaque )
{
    WOW64_CPURESERVED *cpu = NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED];
    BOOL replaced = cpu && (cpu->Flags & WOW64_CPURESERVED_FLAG_RESET_STATE);

    if (replaced) cpu->Flags &= ~WOW64_CPURESERVED_FLAG_RESET_STATE;
    return replaced;
}

static NTSTATUS run_guest( void *args )
{
    static const struct wine_nx_wow64_host host = { read_guest, NULL, unix_guest, context_replaced };
#ifdef __SWITCH__
    static unsigned int trace_runs;
#endif
    struct winebox64_run_params *p = args;
    if (!p || p->version != WINEBOX64_ABI_VERSION || p->size != sizeof(*p))
        return STATUS_INVALID_PARAMETER;
    p->executed = 0;
    /* Use the existing run entry for the handshake: indexing a new entry in
     * an older table would dereference beyond that table before version checks. */
    if (p->operation == winebox64_query_abi) return STATUS_SUCCESS;
    if (p->operation != winebox64_execute) return STATUS_INVALID_PARAMETER;
    {
#ifdef __SWITCH__
        extern void wine_nx_runtime_trace( const char * );
        extern int wine_nx_runtime_verbose;
        unsigned int trace = wine_nx_runtime_verbose
                             ? __atomic_fetch_add( &trace_runs, 1, __ATOMIC_RELAXED ) : 768;
        unsigned char code[12] = {0};
        char message[192];

        /* A WoW64 run starts at the continuation published before the previous
         * syscall. Verbose mode records bounded continuations to locate a hung
         * native block. Quiet runs skip the diagnostic memory read as well. */
        if (trace < 768 && p->context)
        {
            NTSTATUS read_status = read_guest( NULL, p->context->Eip, code, sizeof(code) );
            snprintf( message, sizeof(message),
                      "[BOX64RUN] #%u enter EIP=%08x ESP=%08x EAX=%08x bytes="
                      "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x read=%08x",
                      trace, (unsigned)p->context->Eip, (unsigned)p->context->Esp,
                      (unsigned)p->context->Eax, code[0], code[1], code[2], code[3],
                      code[4], code[5], code[6], code[7], code[8], code[9], code[10],
                      code[11], (unsigned)read_status );
            wine_nx_runtime_trace( message );
        }
#endif
        NTSTATUS status = wine_nx_box64_run( p->context, p->fs_base, &p->gates, &host, NULL,
                                            0, p->budget, &p->executed );
#ifdef __SWITCH__
        if (trace < 768 && p->context)
        {
            snprintf( message, sizeof(message),
                      "[BOX64RUN] #%u leave status=%08x EIP=%08x ESP=%08x EAX=%08x",
                      trace, (unsigned)status, (unsigned)p->context->Eip,
                      (unsigned)p->context->Esp, (unsigned)p->context->Eax );
            wine_nx_runtime_trace( message );
        }
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
static NTSTATUS call_guest_unix( void *args )
{
    struct winebox64_unix_params *p = args;
    if (!p || p->version != WINEBOX64_ABI_VERSION || p->size != sizeof(*p))
        return STATUS_INVALID_PARAMETER;
    return wine_nx_call_ntdll_wow64( p->handle, p->code, p->arguments );
}
/* Defined by the dynarec glue; the interpreter has no translations to drop. */
extern void wine_nx_box64_invalidate( uintptr_t address, size_t size, int destroy ) __attribute__((weak));
static NTSTATUS invalidate_guest_code( void *args )
{
    struct winebox64_invalidate_params *p = args;
    if (!p || p->version != WINEBOX64_ABI_VERSION || p->size != sizeof(*p) || p->address > 0xffffffffu)
        return STATUS_INVALID_PARAMETER;
    if (&wine_nx_box64_invalidate) wine_nx_box64_invalidate( p->address, p->length, !!p->destroy );
    return STATUS_SUCCESS;
}
const unixlib_entry_t wine_nx_winebox64_unix_funcs[] = { run_guest, call_guest_unix, invalidate_guest_code };
