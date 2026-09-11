/* Wine-NX PE32 window timer test. No CRT; kernel32, user32 and ntdll imports.
 * Exit 42 means all groups passed; 0x100 | mask reports failing groups:
 * 1=thread timer, 2=window timer, 4=TIMERPROC, 8=kill and replace,
 * 16=coalescing. Detail codes are printed before exit. */
#include <windows.h>
#include <winternl.h>

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess( HANDLE process, NTSTATUS status );

static void report( const char *label, DWORD code )
{
    static const char hex[] = "0123456789abcdef";
    WCHAR text[160];
    UNICODE_STRING str;
    unsigned int n = 0, i;
    const char *prefix = "[PE32 TEST] ";
    while (*prefix) text[n++] = *prefix++;
    while (*label && n < 120) text[n++] = *label++;
    text[n++] = ' '; text[n++] = '0'; text[n++] = 'x';
    for (i = 0; i < 8; i++) text[n++] = hex[(code >> (28 - i * 4)) & 15];
    text[n] = 0;
    str.Buffer = text; str.Length = n * sizeof(WCHAR);
    str.MaximumLength = (n + 1) * sizeof(WCHAR);
    NtDisplayString( &str );
}

static HWND test_window;
static UINT_PTR thread_id, proc_id;
static DWORD thread_ticks, window_ticks, proc_ticks, stray_ticks;

static LRESULT WINAPI window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_TIMER)
    {
        if (hwnd == test_window && wparam == 7) window_ticks++;
        else stray_ticks++;
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void CALLBACK timer_proc( HWND hwnd, UINT msg, UINT_PTR id, DWORD time )
{
    (void)time;
    if (!hwnd && msg == WM_TIMER && id == proc_id) proc_ticks++;
    else stray_ticks++;
}

/* Dispatch everything that arrives for up to "ms"; stop early once *count >= want. */
static void pump( DWORD ms, const DWORD *count, DWORD want )
{
    DWORD start = GetTickCount();
    MSG msg;

    for (;;)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_TIMER && !msg.hwnd && !msg.lParam)
            {
                if (msg.wParam == thread_id) thread_ticks++;
                else stray_ticks++;
            }
            DispatchMessageW( &msg );
        }
        if (count && *count >= want) return;
        if (GetTickCount() - start >= ms) return;
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 5, QS_ALLINPUT );
    }
}

static DWORD test_thread_timer(void)
{
    DWORD start = GetTickCount(), elapsed;

    if (!(thread_id = SetTimer( NULL, 0, 30, NULL ))) return 1;
    pump( 5000, &thread_ticks, 3 );
    elapsed = GetTickCount() - start;
    report( "thread timer id", (DWORD)thread_id );
    report( "thread timer ticks in ms", elapsed );
    if (thread_ticks < 3) return 2;
    if (elapsed < 60) return 3;
    if (!KillTimer( NULL, thread_id )) return 4;
    return 0;
}

static DWORD test_window_timer(void)
{
    WNDCLASSW cls = {0};

    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW( NULL );
    cls.lpszClassName = L"WineNxTimerTest";
    if (!RegisterClassW( &cls )) return 1;
    test_window = CreateWindowExW( 0, cls.lpszClassName, L"timer", WS_POPUP, 0, 0, 16, 16,
                                   NULL, NULL, cls.hInstance, NULL );
    if (!test_window) return 2;
    if (SetTimer( test_window, 7, 25, NULL ) != 7) return 3;
    pump( 5000, &window_ticks, 3 );
    report( "window timer ticks", window_ticks );
    return window_ticks >= 3 ? 0 : 4;
}

static DWORD test_timer_proc(void)
{
    if (!(proc_id = SetTimer( NULL, 0, 20, timer_proc ))) return 1;
    pump( 5000, &proc_ticks, 3 );
    report( "TIMERPROC calls", proc_ticks );
    if (proc_ticks < 3) return 2;
    return KillTimer( NULL, proc_id ) ? 0 : 3;
}

static DWORD test_kill_and_replace(void)
{
    DWORD ticks;

    if (!KillTimer( test_window, 7 )) return 1;
    pump( 50, NULL, 0 );  /* a tick may already have been queued */
    ticks = window_ticks;
    pump( 200, NULL, 0 );
    if (window_ticks != ticks) return 2;
    if (KillTimer( test_window, 7 )) return 3;
    /* Setting the same id again replaces the timer instead of adding one. */
    if (SetTimer( test_window, 7, 25, NULL ) != 7) return 4;
    if (SetTimer( test_window, 7, 60000, NULL ) != 7) return 5;
    ticks = window_ticks;
    pump( 200, NULL, 0 );
    if (window_ticks != ticks) return 6;
    return KillTimer( test_window, 7 ) ? 0 : 7;
}

static DWORD test_coalescing(void)
{
    DWORD count = 0;
    UINT_PTR id;
    MSG msg;

    if (!(id = SetTimer( NULL, 0, 20, NULL ))) return 1;
    Sleep( 250 );  /* a dozen periods without taking messages */
    while (PeekMessageW( &msg, NULL, WM_TIMER, WM_TIMER, PM_REMOVE ))
        if (!msg.hwnd && msg.wParam == id) count++;
    report( "timer messages after 250 ms asleep (expected 1)", count );
    KillTimer( NULL, id );
    return count == 1 ? 0 : 2;
}

void __stdcall start(void)
{
    DWORD result, mask = 0;

    report( "BEGIN thread timer", 0 ); result = test_thread_timer();
    report( result ? "FAIL thread timer" : "PASS thread timer", result ); if (result) mask |= 1;
    report( "BEGIN window timer", 0 ); result = test_window_timer();
    report( result ? "FAIL window timer" : "PASS window timer", result ); if (result) mask |= 2;
    report( "BEGIN TIMERPROC", 0 ); result = test_timer_proc();
    report( result ? "FAIL TIMERPROC" : "PASS TIMERPROC", result ); if (result) mask |= 4;
    if (!(mask & 2))
    {
        report( "BEGIN kill and replace", 0 ); result = test_kill_and_replace();
        report( result ? "FAIL kill and replace" : "PASS kill and replace", result ); if (result) mask |= 8;
    }
    else mask |= 8;
    report( "BEGIN coalescing", 0 ); result = test_coalescing();
    report( result ? "FAIL coalescing" : "PASS coalescing", result ); if (result) mask |= 16;
    report( "stray timer messages", stray_ticks );
    report( mask ? "FAIL combined mask" : "PASS ALL", mask );
    NtTerminateProcess( (HANDLE)-1, mask ? 0x100 | mask : 42 );
    for (;;) {}
}
