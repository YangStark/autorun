/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_NX_WOW64_BOX64_ENGINE_H
#define WINE_NX_WOW64_BOX64_ENGINE_H
#include <stdint.h>
#include "wow64_box64_bridge.h"

/* Experimental interpreter entry. Guest pointers are identity mapped; the
 * host exception handler must call wine_nx_box64_handle_fault for unresolved
 * data aborts. A nonzero completion PC
 * is for embedding/tests, not a guest-accessible native function pointer.
 * Supports integer/segment/SSE transfer with default MXCSR; x87/AVX and
 * nonempty x87 state are rejected explicitly. This is not a full CPU backend.
 * With both dispatch callbacks NULL, returns success at a gate without changing
 * its stack/EAX; the PE caller dispatches and invokes the engine again.
 * Each invocation owns its emulator, allowing native callbacks to reenter. */
NTSTATUS wine_nx_box64_run( I386_CONTEXT *context, ULONG fs_base,
                          const struct wine_nx_wow64_gates *gates,
                          const struct wine_nx_wow64_host *host, void *opaque,
                          ULONG completion_pc, ULONGLONG budget, ULONGLONG *executed );

/* Called only for unresolved native memory faults. Returns FALSE for a native
 * address or an inactive interpreter; for a 32-bit address during Run it exits
 * the active run with STATUS_ACCESS_VIOLATION. Horizon calls this after libnx
 * has returned from the kernel exception, Linux test hosts from a signal
 * handler. The context is diagnostic: partial instruction effects may remain,
 * so restarting a faulted guest or delivering resumable x86 SEH is unsupported. */
BOOL wine_nx_box64_handle_fault( ULONG_PTR address );
#endif
