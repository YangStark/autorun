/* Wine-NX PE32 thread lifecycle acceptance test. CRT-free; kernel32/ntdll only.
 * Runs the functional groups, then:
 *   exit      - return vs ExitThread, STILL_ACTIVE, joins that wait for the real
 *               end, wait-any/all, OpenThread, closed handles, thread times,
 *               duplicated GetCurrentThread, abandoned and recursive mutexes;
 *   suspended - CREATE_SUSPENDED holds the thread until the last ResumeThread;
 *   rounds    - repeated batches of workers using static TLS (template, TLS
 *               callbacks), dynamic TLS, critical sections, SRW locks, a
 *               condition-variable barrier, recursive mutexes, semaphores and
 *               INIT_ONCE, joined with exit codes verified;
 *   reuse     - TEBs and 32-bit stacks are recycled rather than leaked.
 * Exit 42 means everything passed; 0x100 | mask reports failed groups:
 * 1=time 2=heap 4=TLS 8=file 16=exit 32=suspended 64=rounds 128=reuse.
 * Command line "rounds=N" overrides the default round count. */
#define start functional_start
#include "pe32_functional.c"
#undef start

#define WORKERS 4
#define ITERATIONS 16
#define DEFAULT_ROUNDS 48
#define WAIT_MS 30000
#define MAX_TRACKED 64
#define MAX_DISTINCT 24

/* Static TLS without a CRT: the directory MinGW's tlssup.c would provide. */
ULONG _tls_index = 0;
__attribute__((section(".tls"))) char _tls_start = 0;
__attribute__((section(".tls$ZZZ"))) char _tls_end = 0;
__attribute__((section(".CRT$XLA"))) PIMAGE_TLS_CALLBACK __xl_a = 0;
__attribute__((section(".CRT$XLZ"))) PIMAGE_TLS_CALLBACK __xl_z = 0;
static void NTAPI tls_callback( void *module, DWORD reason, void *reserved );
__attribute__((section(".CRT$XLB"))) PIMAGE_TLS_CALLBACK tls_callback_entry = tls_callback;
__attribute__((used)) const IMAGE_TLS_DIRECTORY32 _tls_used =
{
    (DWORD)(ULONG_PTR)&_tls_start, (DWORD)(ULONG_PTR)&_tls_end, (DWORD)(ULONG_PTR)&_tls_index,
    (DWORD)(ULONG_PTR)(&__xl_a + 1), 0, 0
};

#define TLS_SEED 0x5eed1234u
static _Thread_local DWORD tls_seeded = TLS_SEED;
static _Thread_local DWORD tls_zeroed;
static volatile LONG tls_attaches, tls_detaches, tls_bad_template;

/* MinGW's TEB type is the documented stub; use the TEB32 layout directly. */
static NT_TIB *current_tib(void)
{
    return (NT_TIB *)NtCurrentTeb();
}

static BOOL have_static_tls(void)
{
    return ((void **)NtCurrentTeb())[0x2c / sizeof(void *)] != NULL; /* ThreadLocalStoragePointer */
}

static void NTAPI tls_callback( void *module, DWORD reason, void *reserved )
{
    (void)module; (void)reserved;
    if (reason == DLL_THREAD_ATTACH)
    {
        /* The loader copies the template before calling callbacks. */
        if (!have_static_tls() || tls_seeded != TLS_SEED || tls_zeroed)
            InterlockedIncrement( &tls_bad_template );
        InterlockedIncrement( &tls_attaches );
    }
    else if (reason == DLL_THREAD_DETACH) InterlockedIncrement( &tls_detaches );
}

static DWORD fail_detail;
static void fail( DWORD group, DWORD detail )
{
    if (!fail_detail) fail_detail = detail;
    report( group == 16 ? "exit check failed" : group == 32 ? "suspended check failed" :
            group == 64 ? "round check failed (round << 8 | check)" : "reuse check failed", detail );
}
#define CHECK(group, cond, detail) do { if (!(cond)) { fail( group, detail ); return detail; } } while (0)

/* ---- exit semantics ---------------------------------------------------- */

static HANDLE gate, almost;
static volatile LONG phase, after_exit_thread, ran;

static DWORD WINAPI return_worker( void *arg )
{
    WaitForSingleObject( gate, WAIT_MS );
    return 0x1000 + (DWORD)(ULONG_PTR)arg;
}

static void nested_exit( DWORD code )
{
    ExitThread( code );
}

static DWORD WINAPI exit_thread_worker( void *arg )
{
    WaitForSingleObject( gate, WAIT_MS );
    nested_exit( 0xe0000000u | (DWORD)(ULONG_PTR)arg );
    after_exit_thread = 1; /* must be unreachable */
    return 0;
}

static DWORD WINAPI slow_finish_worker( void *arg )
{
    (void)arg;
    SetEvent( almost );
    Sleep( 150 ); /* wide margin: the main thread checks it is still running */
    phase = 2; /* a join must observe this */
    return 0x2222;
}

static HANDLE shared_mutex;
static DWORD WINAPI abandon_worker( void *arg )
{
    (void)arg;
    if (WaitForSingleObject( shared_mutex, WAIT_MS ) != WAIT_OBJECT_0) return 1;
    return 0x3333; /* exits owning the mutex */
}

static HANDLE self_handle;
static DWORD WINAPI self_dup_worker( void *arg )
{
    (void)arg;
    if (!DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                          &self_handle, 0, FALSE, DUPLICATE_SAME_ACCESS )) return 1;
    SetEvent( almost );
    WaitForSingleObject( gate, WAIT_MS );
    return 0x4444;
}

static HANDLE done_event;
static DWORD WINAPI closed_handle_worker( void *arg )
{
    (void)arg;
    WaitForSingleObject( gate, WAIT_MS );
    SetEvent( done_event );
    return 0x5555;
}

static DWORD test_exit(void)
{
    HANDLE h[3], opened;
    DWORD code, tid, i, attaches, detaches;
    FILETIME created, exited, kernel, user;

    gate = CreateEventW( NULL, TRUE, FALSE, NULL );
    almost = CreateEventW( NULL, TRUE, FALSE, NULL );
    done_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    shared_mutex = CreateMutexW( NULL, FALSE, NULL );
    CHECK( 16, gate && almost && done_event && shared_mutex, 1 );
    attaches = tls_attaches;
    detaches = tls_detaches;

    /* A live thread is STILL_ACTIVE and not signaled, including timed waits. */
    h[0] = CreateThread( NULL, 0, return_worker, (void *)7, 0, &tid );
    CHECK( 16, h[0] && tid, 2 );
    CHECK( 16, GetExitCodeThread( h[0], &code ) && code == STILL_ACTIVE, 3 );
    CHECK( 16, WaitForSingleObject( h[0], 0 ) == WAIT_TIMEOUT, 4 );
    CHECK( 16, WaitForSingleObject( h[0], 50 ) == WAIT_TIMEOUT, 5 );
    CHECK( 16, GetThreadId( h[0] ) == tid, 6 );
    SetEvent( gate );
    CHECK( 16, WaitForSingleObject( h[0], WAIT_MS ) == WAIT_OBJECT_0, 7 );
    CHECK( 16, GetExitCodeThread( h[0], &code ) && code == 0x1007, 8 );
    CHECK( 16, WaitForSingleObject( h[0], 0 ) == WAIT_OBJECT_0, 9 ); /* stays signaled */
    CHECK( 16, GetThreadTimes( h[0], &created, &exited, &kernel, &user ), 10 );
    CHECK( 16, created.dwHighDateTime | created.dwLowDateTime, 11 );
    CHECK( 16, CompareFileTime( &exited, &created ) >= 0, 12 );
    CloseHandle( h[0] );
    ResetEvent( gate );

    /* ExitThread from a nested frame: full 32-bit code, nothing after it runs. */
    h[0] = CreateThread( NULL, 0, exit_thread_worker, (void *)0x42, 0, NULL );
    CHECK( 16, h[0], 13 );
    SetEvent( gate );
    CHECK( 16, WaitForSingleObject( h[0], WAIT_MS ) == WAIT_OBJECT_0, 14 );
    CHECK( 16, GetExitCodeThread( h[0], &code ) && code == 0xe0000042u, 15 );
    CHECK( 16, !after_exit_thread, 16 );
    CloseHandle( h[0] );
    ResetEvent( gate );

    /* The join returns only after the thread's last Windows instruction. */
    phase = 1;
    h[0] = CreateThread( NULL, 0, slow_finish_worker, NULL, 0, NULL );
    CHECK( 16, h[0], 17 );
    CHECK( 16, WaitForSingleObject( almost, WAIT_MS ) == WAIT_OBJECT_0, 18 );
    CHECK( 16, WaitForSingleObject( h[0], 0 ) == WAIT_TIMEOUT, 19 );
    CHECK( 16, WaitForSingleObject( h[0], WAIT_MS ) == WAIT_OBJECT_0, 20 );
    CHECK( 16, phase == 2, 21 );
    CHECK( 16, GetExitCodeThread( h[0], &code ) && code == 0x2222, 22 );
    CloseHandle( h[0] );
    ResetEvent( almost );

    /* Wait-any reports the finished thread's index; wait-all needs every one. */
    h[0] = CreateThread( NULL, 0, return_worker, (void *)0, 0, NULL );
    h[1] = CreateThread( NULL, 0, return_worker, (void *)1, 0, NULL );
    h[2] = CreateThread( NULL, 0, slow_finish_worker, NULL, 0, NULL );
    CHECK( 16, h[0] && h[1] && h[2], 23 );
    CHECK( 16, WaitForMultipleObjects( 2, h, FALSE, 0 ) == WAIT_TIMEOUT, 24 );
    CHECK( 16, WaitForMultipleObjects( 3, h, FALSE, WAIT_MS ) == WAIT_OBJECT_0 + 2, 25 );
    CHECK( 16, WaitForMultipleObjects( 3, h, TRUE, 50 ) == WAIT_TIMEOUT, 26 );
    SetEvent( gate );
    CHECK( 16, WaitForMultipleObjects( 3, h, TRUE, WAIT_MS ) == WAIT_OBJECT_0, 27 );
    for (i = 0; i < 2; i++)
        CHECK( 16, GetExitCodeThread( h[i], &code ) && code == 0x1000 + i, 28 );
    for (i = 0; i < 3; i++) CloseHandle( h[i] );
    ResetEvent( gate );
    ResetEvent( almost );

    /* OpenThread by id, then the creator's handle closes before the exit. */
    h[0] = CreateThread( NULL, 0, closed_handle_worker, NULL, 0, &tid );
    CHECK( 16, h[0], 29 );
    opened = OpenThread( SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, tid );
    CHECK( 16, opened, 30 );
    CHECK( 16, GetThreadId( opened ) == tid, 31 );
    CloseHandle( h[0] );
    SetEvent( gate );
    CHECK( 16, WaitForSingleObject( done_event, WAIT_MS ) == WAIT_OBJECT_0, 32 );
    CHECK( 16, WaitForSingleObject( opened, WAIT_MS ) == WAIT_OBJECT_0, 33 );
    CHECK( 16, GetExitCodeThread( opened, &code ) && code == 0x5555, 34 );
    CloseHandle( opened );
    CHECK( 16, !OpenThread( SYNCHRONIZE, FALSE, 0x7ffffffc ), 35 );
    ResetEvent( gate );

    /* A duplicated GetCurrentThread() is a real handle to the worker. */
    h[0] = CreateThread( NULL, 0, self_dup_worker, NULL, 0, &tid );
    CHECK( 16, h[0], 36 );
    CHECK( 16, WaitForSingleObject( almost, WAIT_MS ) == WAIT_OBJECT_0 && self_handle, 37 );
    CHECK( 16, GetThreadId( self_handle ) == tid, 38 );
    CHECK( 16, WaitForSingleObject( self_handle, 0 ) == WAIT_TIMEOUT, 39 );
    SetEvent( gate );
    CHECK( 16, WaitForSingleObject( self_handle, WAIT_MS ) == WAIT_OBJECT_0, 40 );
    CHECK( 16, GetExitCodeThread( self_handle, &code ) && code == 0x4444, 41 );
    CloseHandle( self_handle );
    CloseHandle( h[0] );
    ResetEvent( gate );
    ResetEvent( almost );

    /* A mutex owned by an exiting thread is abandoned; mutexes are recursive. */
    h[0] = CreateThread( NULL, 0, abandon_worker, NULL, 0, NULL );
    CHECK( 16, h[0], 42 );
    CHECK( 16, WaitForSingleObject( h[0], WAIT_MS ) == WAIT_OBJECT_0, 43 );
    CHECK( 16, GetExitCodeThread( h[0], &code ) && code == 0x3333, 44 );
    CloseHandle( h[0] );
    CHECK( 16, WaitForSingleObject( shared_mutex, WAIT_MS ) == WAIT_ABANDONED, 45 );
    CHECK( 16, WaitForSingleObject( shared_mutex, 0 ) == WAIT_OBJECT_0, 46 );
    CHECK( 16, ReleaseMutex( shared_mutex ) && ReleaseMutex( shared_mutex ), 47 );
    SetLastError( 0 );
    CHECK( 16, !ReleaseMutex( shared_mutex ) && GetLastError() == ERROR_NOT_OWNER, 48 );

    /* Nine joined workers above: each attached and detached through the loader. */
    report( "TLS callback attaches/detaches during exit group",
            (tls_attaches - attaches) << 16 | (tls_detaches - detaches) );
    CHECK( 16, tls_attaches - attaches == 9 && tls_detaches - detaches == 9, 49 );
    CloseHandle( gate );
    CloseHandle( almost );
    CloseHandle( done_event );
    CloseHandle( shared_mutex );
    return 0;
}

/* ---- suspended start --------------------------------------------------- */

static DWORD WINAPI suspended_worker( void *arg )
{
    (void)arg;
    ran = 1;
    return 0x6666;
}

static DWORD test_suspended(void)
{
    HANDLE h;
    DWORD code;

    ran = 0;
    h = CreateThread( NULL, 0, suspended_worker, NULL, CREATE_SUSPENDED, NULL );
    CHECK( 32, h, 1 );
    Sleep( 50 );
    CHECK( 32, !ran, 2 );
    CHECK( 32, GetExitCodeThread( h, &code ) && code == STILL_ACTIVE, 3 );
    CHECK( 32, WaitForSingleObject( h, 0 ) == WAIT_TIMEOUT, 4 );
    CHECK( 32, SuspendThread( h ) == 1, 5 );
    CHECK( 32, ResumeThread( h ) == 2, 6 );
    Sleep( 30 );
    CHECK( 32, !ran, 7 );
    CHECK( 32, ResumeThread( h ) == 1, 8 );
    CHECK( 32, WaitForSingleObject( h, WAIT_MS ) == WAIT_OBJECT_0, 9 );
    CHECK( 32, ran, 10 );
    CHECK( 32, GetExitCodeThread( h, &code ) && code == 0x6666, 11 );
    CloseHandle( h );
    return 0;
}

/* ---- repeated rounds --------------------------------------------------- */

struct round_state
{
    DWORD round, slot;
    HANDLE start, sem, kmutex;
    CRITICAL_SECTION cs;
    SRWLOCK srw, cv_lock;
    CONDITION_VARIABLE cv;
    INIT_ONCE once;
    volatile LONG crit_counter, srw_counter, mutex_counter, atomic_counter, arrived, init_calls;
    DWORD worker_error[WORKERS];
    ULONG_PTR teb[WORKERS], stack[WORKERS];
};
static struct round_state rs;
static DWORD round_token;

static BOOL CALLBACK init_once_cb( INIT_ONCE *once, void *param, void **context )
{
    (void)once; (void)param;
    InterlockedIncrement( &rs.init_calls );
    Sleep( 20 ); /* others must block in InitOnceExecuteOnce meanwhile */
    *context = &round_token;
    return TRUE;
}

static DWORD worker_body( DWORD index )
{
    DWORD unique = (rs.round << 8) | index, i;
    void *dynamic = (void *)(ULONG_PTR)(0x10000 | unique << 4), *context = NULL;
    NT_TIB *tib = current_tib();

    rs.teb[index] = (ULONG_PTR)tib->Self;
    rs.stack[index] = (ULONG_PTR)tib->StackBase;
    if (!have_static_tls()) return 20;
    if (tls_seeded != TLS_SEED || tls_zeroed) return 1;
    tls_seeded = unique;
    tls_zeroed = ~unique;
    if (TlsGetValue( rs.slot )) return 2;
    if (!TlsSetValue( rs.slot, dynamic )) return 3;
    if (WaitForSingleObject( rs.start, WAIT_MS ) != WAIT_OBJECT_0) return 4;
    if (!InitOnceExecuteOnce( &rs.once, init_once_cb, NULL, &context )) return 5;
    if (context != &round_token) return 6;

    for (i = 0; i < ITERATIONS; i++)
    {
        EnterCriticalSection( &rs.cs );
        rs.crit_counter = rs.crit_counter + 1;
        LeaveCriticalSection( &rs.cs );

        AcquireSRWLockExclusive( &rs.srw );
        rs.srw_counter = rs.srw_counter + 1;
        ReleaseSRWLockExclusive( &rs.srw );

        if (WaitForSingleObject( rs.kmutex, WAIT_MS ) != WAIT_OBJECT_0) return 7;
        if ((i & 3) == 0 && WaitForSingleObject( rs.kmutex, 0 ) != WAIT_OBJECT_0) return 8;
        rs.mutex_counter = rs.mutex_counter + 1;
        if ((i & 3) == 0 && !ReleaseMutex( rs.kmutex )) return 9;
        if (!ReleaseMutex( rs.kmutex )) return 10;

        InterlockedIncrement( &rs.atomic_counter );
        if (tls_seeded != unique || tls_zeroed != ~unique) return 11;
        if (TlsGetValue( rs.slot ) != dynamic) return 12;
    }

    /* Condition-variable barrier: everyone leaves together. */
    AcquireSRWLockExclusive( &rs.cv_lock );
    if (++rs.arrived == WORKERS) WakeAllConditionVariable( &rs.cv );
    while (rs.arrived < WORKERS)
    {
        if (!SleepConditionVariableSRW( &rs.cv, &rs.cv_lock, WAIT_MS, 0 ))
        {
            ReleaseSRWLockExclusive( &rs.cv_lock );
            return 13;
        }
    }
    ReleaseSRWLockExclusive( &rs.cv_lock );
    if (!ReleaseSemaphore( rs.sem, 1, NULL )) return 14;
    return 0;
}

static DWORD exit_code_for( DWORD round, DWORD index )
{
    return 0xc0de0000u | (round & 0xfff) << 4 | index;
}

static DWORD WINAPI round_worker( void *arg )
{
    DWORD index = (DWORD)(ULONG_PTR)arg;

    rs.worker_error[index] = worker_body( index );
    if (index & 1) ExitThread( exit_code_for( rs.round, index ) );
    return exit_code_for( rs.round, index );
}

static ULONG_PTR seen_tebs[MAX_TRACKED], seen_stacks[MAX_TRACKED];
static DWORD seen_teb_count, seen_stack_count;

static void track( ULONG_PTR *set, DWORD *count, ULONG_PTR value )
{
    DWORD i;
    for (i = 0; i < *count; i++) if (set[i] == value) return;
    if (*count < MAX_TRACKED) set[(*count)++] = value;
    else *count = MAX_TRACKED + 1;
}

static DWORD run_round( DWORD round )
{
    HANDLE h[WORKERS];
    DWORD i, code, attaches = tls_attaches, detaches = tls_detaches;
    void *main_value = (void *)(ULONG_PTR)(0xa000 + round * 16);

    for (i = 0; i < WORKERS; i++) { rs.worker_error[i] = 0xffff; rs.teb[i] = rs.stack[i] = 0; }
    rs.round = round;
    rs.crit_counter = rs.srw_counter = rs.mutex_counter = rs.atomic_counter = 0;
    rs.arrived = rs.init_calls = 0;
    InitializeSRWLock( &rs.srw );
    InitializeSRWLock( &rs.cv_lock );
    InitializeConditionVariable( &rs.cv );
    InitOnceInitialize( &rs.once );
    rs.slot = TlsAlloc();
    CHECK( 64, rs.slot != TLS_OUT_OF_INDEXES, round << 8 | 1 );
    CHECK( 64, TlsSetValue( rs.slot, main_value ), round << 8 | 2 );

    for (i = 0; i < WORKERS; i++)
    {
        h[i] = CreateThread( NULL, 0, round_worker, (void *)(ULONG_PTR)i, 0, NULL );
        if (!h[i]) report( "CreateThread GetLastError", GetLastError() );
        CHECK( 64, h[i], round << 8 | 3 );
    }
    for (i = 0; i < WORKERS; i++)
        CHECK( 64, GetExitCodeThread( h[i], &code ) && (code == STILL_ACTIVE || code == exit_code_for( round, i )),
               round << 8 | 4 );
    SetEvent( rs.start );
    for (i = 0; i < WORKERS; i++)
        CHECK( 64, WaitForSingleObject( rs.sem, WAIT_MS ) == WAIT_OBJECT_0, round << 8 | 5 );

    /* Alternate the join primitive between rounds. */
    if (round & 1)
        CHECK( 64, WaitForMultipleObjects( WORKERS, h, TRUE, WAIT_MS ) == WAIT_OBJECT_0, round << 8 | 6 );
    else
        for (i = 0; i < WORKERS; i++)
            CHECK( 64, WaitForSingleObject( h[i], WAIT_MS ) == WAIT_OBJECT_0, round << 8 | 7 );

    for (i = 0; i < WORKERS; i++)
    {
        if (rs.worker_error[i]) report( "worker error (index << 16 | code)", i << 16 | rs.worker_error[i] );
        CHECK( 64, !rs.worker_error[i], round << 8 | 8 );
        CHECK( 64, GetExitCodeThread( h[i], &code ) && code == exit_code_for( round, i ), round << 8 | 9 );
        CHECK( 64, CloseHandle( h[i] ), round << 8 | 10 );
        track( seen_tebs, &seen_teb_count, rs.teb[i] );
        track( seen_stacks, &seen_stack_count, rs.stack[i] );
    }
    CHECK( 64, rs.crit_counter == WORKERS * ITERATIONS, round << 8 | 11 );
    CHECK( 64, rs.srw_counter == WORKERS * ITERATIONS, round << 8 | 12 );
    CHECK( 64, rs.mutex_counter == WORKERS * ITERATIONS, round << 8 | 13 );
    CHECK( 64, rs.atomic_counter == WORKERS * ITERATIONS, round << 8 | 14 );
    CHECK( 64, rs.init_calls == 1, round << 8 | 15 );
    CHECK( 64, tls_attaches - attaches == WORKERS && tls_detaches - detaches == WORKERS, round << 8 | 16 );
    CHECK( 64, TlsGetValue( rs.slot ) == main_value, round << 8 | 17 );
    CHECK( 64, tls_seeded == TLS_SEED + 1 && tls_zeroed == 0x51, round << 8 | 18 );
    CHECK( 64, TlsFree( rs.slot ), round << 8 | 19 );
    CHECK( 64, ResetEvent( rs.start ), round << 8 | 20 );
    CHECK( 64, WaitForSingleObject( rs.kmutex, 0 ) == WAIT_OBJECT_0 && ReleaseMutex( rs.kmutex ), round << 8 | 21 );
    return 0;
}

static DWORD parse_rounds(void)
{
    const WCHAR *p = GetCommandLineW();
    DWORD value = 0;

    for (; p && *p; p++)
    {
        if (p[0] != 'r' || p[1] != 'o' || p[2] != 'u' || p[3] != 'n' || p[4] != 'd' ||
            p[5] != 's' || p[6] != '=') continue;
        for (p += 7; *p >= '0' && *p <= '9'; p++) value = value * 10 + (*p - '0');
        break;
    }
    return value >= 1 && value <= 10000 ? value : DEFAULT_ROUNDS;
}

static DWORD test_rounds( DWORD rounds )
{
    DWORD round, result = 0, begin = GetTickCount();

    /* Main thread's own static TLS must stay private across all rounds. */
    tls_seeded = TLS_SEED + 1;
    tls_zeroed = 0x51;
    rs.start = CreateEventW( NULL, TRUE, FALSE, NULL );
    rs.sem = CreateSemaphoreW( NULL, 0, WORKERS, NULL );
    rs.kmutex = CreateMutexW( NULL, FALSE, NULL );
    CHECK( 64, rs.start && rs.sem && rs.kmutex, 1 );
    InitializeCriticalSection( &rs.cs );
    for (round = 1; round <= rounds && !result; round++)
    {
        result = run_round( round );
        if (!result && (round % 8 == 0 || round == rounds))
            report( "rounds done (round << 16 | seconds)", round << 16 | ((GetTickCount() - begin) / 1000) );
    }
    DeleteCriticalSection( &rs.cs );
    CloseHandle( rs.start );
    CloseHandle( rs.sem );
    CloseHandle( rs.kmutex );
    return result;
}

void __stdcall start(void)
{
    DWORD result, mask = 0, rounds = parse_rounds(), begin = GetTickCount();

    report( "BEGIN time", 0 ); result = test_time();
    report( result ? "FAIL time" : "PASS time", result ); if (result) mask |= 1;
    report( "BEGIN heap", 0 ); result = test_heap();
    report( result ? "FAIL heap" : "PASS heap", result ); if (result) mask |= 2;
    report( "BEGIN TLS single thread", 0 ); result = test_tls();
    report( result ? "FAIL TLS" : "PASS TLS", result ); if (result) mask |= 4;
    report( "BEGIN file I/O", 0 ); result = test_file();
    report( result ? "FAIL file I/O" : "PASS file I/O", result ); if (result) mask |= 8;
    if (!have_static_tls())
    {
        report( "FAIL static TLS: no ThreadLocalStoragePointer on the main thread", _tls_index );
        mask |= 4;
    }
    else if (tls_seeded != TLS_SEED || tls_zeroed)
    {
        report( "FAIL static TLS template on the main thread", tls_seeded );
        mask |= 4;
    }
    else report( "PASS static TLS template, index", _tls_index );
    if (mask & 4) goto done;

    report( "BEGIN exit semantics", 0 ); result = test_exit();
    report( result ? "FAIL exit semantics" : "PASS exit semantics", result ); if (result) mask |= 16;
    if (!result)
    {
        report( "BEGIN suspended start", 0 ); result = test_suspended();
        report( result ? "FAIL suspended start" : "PASS suspended start", result ); if (result) mask |= 32;
    }
    if (!mask)
    {
        report( "BEGIN rounds (workers << 16 | rounds)", WORKERS << 16 | rounds );
        result = test_rounds( rounds );
        report( result ? "FAIL rounds" : "PASS rounds", result ); if (result) mask |= 64;
    }
    if (!mask)
    {
        /* Reused TEBs and stacks stay few; leaked ones grow with every thread. */
        report( "distinct worker TEBs", seen_teb_count );
        report( "distinct worker stacks", seen_stack_count );
        if (seen_teb_count > MAX_DISTINCT || seen_stack_count > MAX_DISTINCT) mask |= 128;
        report( mask & 128 ? "FAIL reuse" : "PASS reuse", seen_teb_count << 16 | seen_stack_count );
    }
    report( "TLS callback template mismatches", tls_bad_template );
    if (tls_bad_template) mask |= 64;
done:
    report( "elapsed seconds", (GetTickCount() - begin) / 1000 );
    report( mask ? "FAIL combined mask" : "PASS ALL", mask ? mask : 0 );
    if (fail_detail) report( "first failing detail", fail_detail );
    NtTerminateProcess( (HANDLE)-1, mask ? 0x100 | mask : 42 );
    for (;;) {}
}
