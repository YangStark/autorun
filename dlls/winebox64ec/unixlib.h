/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINEBOX64EC_UNIXLIB_H
#define WINEBOX64EC_UNIXLIB_H

/* This interface crosses the PE/Unix boundary.  Keep every address and opaque
 * token as an explicit 64-bit integer: ARM64EC pointer spelling must not make
 * the layout depend on which side includes this file. */
#define WINEBOX64EC_ABI_VERSION 2

enum winebox64ec_calls
{
    winebox64ec_query,
    winebox64ec_process_init,
    winebox64ec_thread_init,
    winebox64ec_run,
    winebox64ec_notify,
    winebox64ec_reset,
    winebox64ec_thread_term,
    winebox64ec_process_term
};

enum winebox64ec_entry_kind
{
    WINEBOX64EC_ENTRY_LIVE = 1,       /* registers were captured at an EC/x64 edge */
    WINEBOX64EC_ENTRY_CONTINUE = 2    /* ContextAmd64 is authoritative */
};

enum winebox64ec_exit_kind
{
    WINEBOX64EC_EXIT_NONE,
    WINEBOX64EC_EXIT_EC_TARGET,
    WINEBOX64EC_EXIT_SYSCALL,
    WINEBOX64EC_EXIT_EXCEPTION
};

enum winebox64ec_notification
{
    WINEBOX64EC_NOTIFY_FLUSH,
    WINEBOX64EC_NOTIFY_FLUSH_HEAVY,
    WINEBOX64EC_NOTIFY_DIRTY,
    WINEBOX64EC_NOTIFY_READ_FILE,
    WINEBOX64EC_NOTIFY_MAP,
    WINEBOX64EC_NOTIFY_ALLOC,
    WINEBOX64EC_NOTIFY_FREE,
    WINEBOX64EC_NOTIFY_PROTECT,
    WINEBOX64EC_NOTIFY_UNMAP
};

#define WINEBOX64EC_CAP_SSE2 0x00000001

struct winebox64ec_query_params
{
    ULONG version;
    ULONG size;
    ULONG capabilities;
    ULONG context_size;
};

struct winebox64ec_process_params
{
    ULONG version;
    ULONG size;
    ULONGLONG process;                 /* out: native process opaque */
    ULONGLONG peb;
    ULONGLONG flags;
    ULONGLONG dispatch_ret;
};

struct winebox64ec_thread_params
{
    ULONG version;
    ULONG size;
    ULONGLONG cpu_area;
    ULONGLONG teb;
    ULONGLONG thread;                  /* out: persistent native thread opaque */
    ULONGLONG suspend_doorbell;
};

struct winebox64ec_run_params
{
    ULONG version;
    ULONG size;
    ULONG entry_kind;
    ULONG exit_kind;                   /* out: enum winebox64ec_exit_kind */
    ULONGLONG thread;
    ULONGLONG context;                 /* canonical ARM64EC_NT_CONTEXT */
    ULONGLONG target;                  /* out: EC target, normally context->Rip */
    ULONGLONG exception_record;        /* out: EXCEPTION_RECORD, if any */
    ULONGLONG executed;                /* out: guest instructions in this run */
    ULONGLONG detail0;                 /* exit-specific fixed-width payload */
    ULONGLONG detail1;
};

struct winebox64ec_notify_params
{
    ULONG version;
    ULONG size;
    ULONG notification;
    ULONG is_post;
    ULONGLONG address;
    ULONGLONG length;
    ULONGLONG argument0;
    ULONGLONG argument1;
    LONG status;
    ULONG reserved;
};

struct winebox64ec_reset_params
{
    ULONG version;
    ULONG size;
    ULONGLONG thread;
    ULONGLONG exception_record;
    ULONGLONG guest_context;
    ULONGLONG native_context;
    ULONG handled;
    ULONG reserved;
};

struct winebox64ec_term_params
{
    ULONG version;
    ULONG size;
    ULONGLONG object;
    ULONGLONG handle;
    LONG exit_code;
    ULONG is_post;
    LONG status;
    ULONG reserved;
};

C_ASSERT( sizeof(struct winebox64ec_query_params) == 16 );
C_ASSERT( sizeof(struct winebox64ec_process_params) == 40 );
C_ASSERT( sizeof(struct winebox64ec_thread_params) == 40 );
C_ASSERT( sizeof(struct winebox64ec_run_params) == 72 );
C_ASSERT( sizeof(struct winebox64ec_notify_params) == 56 );
C_ASSERT( sizeof(struct winebox64ec_reset_params) == 48 );
C_ASSERT( sizeof(struct winebox64ec_term_params) == 40 );

#endif
