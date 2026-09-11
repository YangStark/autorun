/* Two real x86 workers. Completion uses events, not thread termination: the
 * current Horizon thread-exit implementation is a separate milestone. */
#define start functional_start
#include "pe32_functional.c"
#undef start

static HANDLE ready[2], done[2], start_event, park_event, test_mutex;
static DWORD tls_slot;
static volatile LONG counter;
static volatile DWORD worker_result[2];

static DWORD WINAPI worker( void *arg )
{
    DWORD id = (DWORD)(ULONG_PTR)arg, i, result = 0;
    void *value = (void *)(ULONG_PTR)(0xabc000 + id * 16);
    if (TlsGetValue(tls_slot)) result = 1;
    if (!TlsSetValue(tls_slot, value)) result = 2;
    if (!SetEvent(ready[id])) result = 3;
    if (WaitForSingleObject(start_event, 10000) != WAIT_OBJECT_0) result = 4;
    for (i = 0; !result && i < 32; i++)
    {
        if (WaitForSingleObject(test_mutex, 10000) != WAIT_OBJECT_0) { result = 5; break; }
        counter = counter + 1;
        if (!ReleaseMutex(test_mutex)) { result = 6; break; }
        if (TlsGetValue(tls_slot) != value) { result = 7; break; }
    }
    worker_result[id] = result;
    SetEvent(done[id]);
    /* Keep the worker alive so this test makes no claim about thread exit. */
    for (;;) WaitForSingleObject(park_event, INFINITE);
}

static DWORD test_workers(void)
{
    HANDLE threads[2] = {0};
    DWORD i, result = 0;
    tls_slot = TlsAlloc();
    if (tls_slot == TLS_OUT_OF_INDEXES) return 1;
    if (!TlsSetValue(tls_slot, (void *)0xabcdef)) return 2;
    start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    park_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    test_mutex = CreateMutexW(NULL, FALSE, NULL);
    if (!start_event || !park_event || !test_mutex) return 3;
    if (WaitForSingleObject(start_event, 0) != WAIT_TIMEOUT) return 4;
    for (i = 0; i < 2; i++)
    {
        ready[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        done[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!ready[i] || !done[i]) return 5;
        threads[i] = CreateThread(NULL, 0, worker, (void *)(ULONG_PTR)i, 0, NULL);
        if (!threads[i]) { report("CreateThread GetLastError", GetLastError()); return 6; }
    }
    for (i = 0; i < 2; i++)
        if (WaitForSingleObject(ready[i], 10000) != WAIT_OBJECT_0) result = 7;
    if (!SetEvent(start_event)) result = 8;
    for (i = 0; i < 2; i++)
        if (WaitForSingleObject(done[i], 10000) != WAIT_OBJECT_0) result = 9;
    if (result) return result; /* Avoid freeing resources still used by workers. */
    for (i = 0; i < 2; i++)
    {
        report(i ? "worker 1 result" : "worker 0 result", worker_result[i]);
        if (worker_result[i]) result = 0x10 + i;
        if (WaitForSingleObject(done[i], 0) != WAIT_TIMEOUT) result = 0x12;
    }
    if (!result && counter != 64) result = 0x13;
    if (TlsGetValue(tls_slot) != (void *)0xabcdef) result = 0x14;
    report("mutex protected counter (expected 64)", counter);
    /* Workers retain park_event; process termination owns cleanup for now. */
    return result;
}

void __stdcall start(void)
{
    DWORD result, mask = 0;
    report("BEGIN time", 0); result = test_time();
    report(result ? "FAIL time" : "PASS time", result); if (result) mask |= 1;
    report("BEGIN heap", 0); result = test_heap();
    report(result ? "FAIL heap" : "PASS heap", result); if (result) mask |= 2;
    report("BEGIN TLS single thread", 0); result = test_tls();
    report(result ? "FAIL TLS" : "PASS TLS", result); if (result) mask |= 4;
    report("BEGIN file I/O", 0); result = test_file();
    report(result ? "FAIL file I/O" : "PASS file I/O", result); if (result) mask |= 8;
    report("BEGIN workers/TLS isolation/events/mutex", 0); result = test_workers();
    report(result ? "FAIL workers" : "PASS workers", result); if (result) mask |= 16;
    report(mask ? "FAIL combined mask" : "PASS ALL", mask);
    NtTerminateProcess((HANDLE)-1, mask ? 0x100 | mask : 42);
    for (;;) {}
}
