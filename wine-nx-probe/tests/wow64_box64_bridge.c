/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wow64_box64_bridge.h"
#include "horizon_wow64.h"

static const struct wine_nx_wow64_gates gates = {0x10000000, 0x10000010};
struct fixture
{
    I386_CONTEXT context;
    ULONG stack[16];
    ULONG number, args, calls, replaced_queries;
    ULONGLONG handle;
    BOOL read_fault, redirect, reenter, replaced;
};

static NTSTATUS read_guest( void *opaque, ULONG address, void *buffer, SIZE_T size )
{
    struct fixture *f = opaque;
    if (f->read_fault || address < 0x1000 || address - 0x1000 > sizeof(f->stack) ||
        size > sizeof(f->stack) - (address - 0x1000)) return STATUS_ACCESS_VIOLATION;
    memcpy( buffer, (char *)f->stack + address - 0x1000, size );
    return STATUS_SUCCESS;
}

static NTSTATUS native_syscall( void *opaque, ULONG number, ULONG args );
static NTSTATUS native_unix_call( void *opaque, ULONGLONG handle, ULONG code, ULONG args )
{
    struct fixture *f = opaque;
    f->calls++; f->handle = handle; f->number = code; f->args = args;
    assert( f->context.Eip == f->stack[0] );
    assert( f->context.Esp == 0x1014 );
    return STATUS_INVALID_HANDLE;
}
static BOOL context_replaced( void *opaque );
static const struct wine_nx_wow64_host host = {read_guest, native_syscall, native_unix_call, NULL};
static const struct wine_nx_wow64_host reset_host = {read_guest, native_syscall, native_unix_call,
                                                     context_replaced};

/* Models WoW64's RESET_STATE flag: set by a full context replacement, consumed once. */
static BOOL context_replaced( void *opaque )
{
    struct fixture *f = opaque;
    BOOL replaced = f->replaced;

    f->replaced_queries++;
    f->replaced = FALSE;
    return replaced;
}

static NTSTATUS native_syscall( void *opaque, ULONG number, ULONG args )
{
    struct fixture *f = opaque;
    f->calls++; f->number = number; f->args = args;
    assert( f->context.Eip == f->stack[0] );
    assert( f->context.Esp == 0x1004 );
    if (f->reenter)
    {
        f->reenter = FALSE;
        f->context.Eip = gates.unix_call;
        f->context.Esp = 0x1000;
        assert( !wine_nx_wow64_dispatch_gate( &f->context, &gates, &host, f ) );
    }
    if (f->redirect)
    {
        /* NtContinue to a C++ catch continuation: Eax is RtlUnwind's return value. */
        f->context.Eip = 0x20000000;
        f->context.Esp = 0x2020;
        f->context.Ecx = 0xabcdef01;
        f->context.Eax = 0x0badcafe;
        f->replaced = TRUE;
    }
    return (NTSTATUS)0x12345678;
}

static struct fixture initial(void)
{
    struct fixture f = {0};
    f.context.ContextFlags = CONTEXT_I386_ALL;
    f.context.Eip = gates.syscall;
    f.context.Esp = 0x1000;
    f.context.Eax = 0x1007; /* preserve win32u table selector bits */
    f.context.SegFs = 0x53;
    f.context.EFlags = 0x202;
    f.context.ExtendedRegisters[0] = 0xa5;
    f.stack[0] = 0x18000000;
    f.stack[1] = 0x76543210;
    f.stack[2] = 0xfedcba98;
    f.stack[3] = 17;
    f.stack[4] = 0x10203040;
    return f;
}

static void test_gates(void)
{
    struct fixture f = initial();
    I386_CONTEXT before;
    struct wine_nx_wow64_host missing = host;
    assert( !wine_nx_wow64_dispatch_gate( &f.context, &gates, &host, &f ) );
    assert( f.calls == 1 && f.number == 0x1007 && f.args == 0x1008 );
    assert( f.context.Eax == 0x12345678 && f.context.Esp == 0x1004 );
    assert( f.context.SegFs == 0x53 && f.context.EFlags == 0x202 );
    assert( f.context.ExtendedRegisters[0] == 0xa5 );

    f = initial(); f.context.Eip = gates.unix_call;
    assert( !wine_nx_wow64_dispatch_gate( &f.context, &gates, &host, &f ) );
    assert( f.handle == (ULONGLONG)0xfedcba9876543210ULL );
    assert( f.number == 17 && f.args == 0x10203040 );
    assert( f.context.Eax == (ULONG)STATUS_INVALID_HANDLE );

    f = initial(); f.redirect = TRUE;
    assert( !wine_nx_wow64_dispatch_gate( &f.context, &gates, &host, &f ) );
    assert( f.context.Eip == 0x20000000 && f.context.Esp == 0x2020 );
    assert( f.context.Ecx == 0xabcdef01 );

    /* With RESET_STATE reporting, a replaced context keeps its own Eax... */
    f = initial(); f.redirect = TRUE;
    assert( !wine_nx_wow64_dispatch_gate( &f.context, &gates, &reset_host, &f ) );
    assert( f.context.Eip == 0x20000000 && f.context.Esp == 0x2020 );
    assert( f.context.Eax == 0x0badcafe && f.replaced_queries == 1 && !f.replaced );
    /* ...and an ordinary call still returns its status in Eax. */
    f = initial();
    assert( !wine_nx_wow64_dispatch_gate( &f.context, &gates, &reset_host, &f ) );
    assert( f.context.Eax == 0x12345678 && f.context.Esp == 0x1004 && f.replaced_queries == 1 );
    f = initial(); f.context.Eip = gates.unix_call;
    assert( !wine_nx_wow64_dispatch_gate( &f.context, &gates, &reset_host, &f ) );
    assert( f.context.Eax == (ULONG)STATUS_INVALID_HANDLE );

    f = initial(); f.reenter = TRUE;
    assert( !wine_nx_wow64_dispatch_gate( &f.context, &gates, &host, &f ) );
    assert( f.calls == 2 && f.context.Esp == 0x1014 );

    f = initial(); f.read_fault = TRUE; before = f.context;
    assert( wine_nx_wow64_dispatch_gate( &f.context, &gates, &host, &f ) == STATUS_ACCESS_VIOLATION );
    assert( !memcmp( &before, &f.context, sizeof(before) ) && !f.calls );

    f = initial(); f.context.Esp = 0xfffffffc; before = f.context;
    assert( wine_nx_wow64_dispatch_gate( &f.context, &gates, &host, &f ) == STATUS_ACCESS_VIOLATION );
    assert( !memcmp( &before, &f.context, sizeof(before) ) && !f.calls );

    f = initial(); f.context.Eip++; before = f.context;
    assert( wine_nx_wow64_dispatch_gate( &f.context, &gates, &host, &f ) == STATUS_ILLEGAL_INSTRUCTION );
    assert( !memcmp( &before, &f.context, sizeof(before) ) && !f.calls );

    f = initial(); before = f.context; missing.syscall = NULL;
    assert( wine_nx_wow64_dispatch_gate( &f.context, &gates, &missing, &f ) == STATUS_NOT_SUPPORTED );
    assert( !memcmp( &before, &f.context, sizeof(before) ) && !f.calls );
}

static void test_context(void)
{
    WOW64_CPURESERVED cpu = {0, IMAGE_FILE_MACHINE_I386};
    I386_CONTEXT saved = initial().context, context = {0}, before;
    context.ContextFlags = CONTEXT_I386_INTEGER;
    context.Eax = 42; context.Eip = 0xdeadbeef;
    assert( !horizon_transfer_i386_context( &cpu, &saved, &context, TRUE ) );
    assert( saved.Eax == 42 && saved.Eip == gates.syscall && !cpu.Flags );
    context.ContextFlags = CONTEXT_I386_CONTROL;
    context.Eip = 0x1234; context.Esp = 0x5678;
    assert( !horizon_transfer_i386_context( &cpu, &saved, &context, TRUE ) );
    assert( saved.Eip == 0x1234 && saved.Esp == 0x5678 && saved.Eax == 42 );
    assert( cpu.Flags & WOW64_CPURESERVED_FLAG_RESET_STATE );
    memset( &context, 0, sizeof(context) );
    context.ContextFlags = CONTEXT_I386_EXTENDED_REGISTERS | CONTEXT_I386_SEGMENTS;
    assert( !horizon_transfer_i386_context( &cpu, &saved, &context, FALSE ) );
    assert( context.SegFs == 0x53 && context.ExtendedRegisters[0] == 0xa5 && !context.Eip );
    /* x86 RtlCaptureContext writes selectors with 16-bit moves into a stack
     * CONTEXT: stale upper bits must not reach the saved state. */
    memset( &context, 0xcc, sizeof(context) );
    context.ContextFlags = CONTEXT_I386_CONTROL | CONTEXT_I386_SEGMENTS;
    context.SegCs = 0xdead0023; context.SegSs = 0xbeef002b; context.SegDs = 0x1234002b;
    context.SegEs = 0xffff002b; context.SegFs = 0x00010053; context.SegGs = 0x8000002b;
    assert( !horizon_transfer_i386_context( &cpu, &saved, &context, TRUE ) );
    assert( saved.SegCs == 0x23 && saved.SegSs == 0x2b && saved.SegDs == 0x2b );
    assert( saved.SegEs == 0x2b && saved.SegFs == 0x53 && saved.SegGs == 0x2b );
    before = saved; context.ContextFlags = CONTEXT_I386_XSTATE;
    assert( horizon_transfer_i386_context( &cpu, &saved, &context, TRUE ) == STATUS_NOT_SUPPORTED );
    assert( !memcmp( &before, &saved, sizeof(saved) ) );
    assert( horizon_transfer_i386_context( NULL, &saved, &context, TRUE ) == STATUS_INVALID_PARAMETER );
    cpu.Machine = IMAGE_FILE_MACHINE_ARMNT;
    assert( horizon_transfer_i386_context( &cpu, &saved, &context, TRUE ) == STATUS_INVALID_PARAMETER );
}

int main(void)
{
    test_gates();
    test_context();
    puts( "WoW64 bridge: gate ABI, callbacks, reentry, replaced contexts, faults, context groups and selectors passed" );
    return 0;
}
