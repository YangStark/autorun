/* Wine-NX PE message queue test. No CRT; kernel32, user32 and ntdll imports.
 * Exit 42 means all groups passed; 0x100 | mask reports failing groups:
 * 1=cross-thread SendMessage, 2=SendMessageTimeout, 4=SendNotifyMessage,
 * 8=nested send, 16=ReplyMessage, 32=SendMessageCallback,
 * 64=MsgWaitForMultipleObjects, 128=GetQueueStatus, 256=thread quit,
 * 512=clipboard and registered formats. Detail codes are printed before exit. */
#include <windows.h>
#include <winternl.h>

#ifdef _WIN64
#define PE_TEST_PREFIX "[PE64 TEST] "
#else
#define PE_TEST_PREFIX "[PE32 TEST] "
#endif

#include "pe_test_io.h"

static void report( const char *label, DWORD code )
{
    static const char hex[] = "0123456789abcdef";
    WCHAR text[160];
    UNICODE_STRING str;
    unsigned int n = 0, i;
    const char *prefix = PE_TEST_PREFIX;
    while (*prefix) text[n++] = *prefix++;
    while (*label && n < 120) text[n++] = *label++;
    text[n++] = ' '; text[n++] = '0'; text[n++] = 'x';
    for (i = 0; i < 8; i++) text[n++] = hex[(code >> (28 - i * 4)) & 15];
    text[n] = 0;
    str.Buffer = text; str.Length = n * sizeof(WCHAR);
    str.MaximumLength = (n + 1) * sizeof(WCHAR);
    pe_test_display_string( &str );
}

static HWND a_window, b_window;
static HANDLE b_ready;
static volatile LONG notify_count, callback_calls;
static ULONG_PTR callback_data;
static LRESULT callback_result;
static DWORD b_posted;

static LRESULT WINAPI b_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_USER + 1: return wparam * 2;
    case WM_USER + 2: Sleep( 300 ); return InSendMessage() ? 1 : 0xbad;
    case WM_USER + 3: InterlockedIncrement( &notify_count ); return 0;
    case WM_USER + 4: return SendMessageW( a_window, WM_USER + 10, wparam, 0 ) + 1;
    case WM_USER + 5: ReplyMessage( 77 ); Sleep( 50 ); return 88;
    case WM_USER + 6: return 123;
    case WM_USER + 7: Sleep( 50 ); PostMessageW( a_window, WM_USER + 11, 0, 0 ); b_posted = GetTickCount(); return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static LRESULT WINAPI a_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_USER + 10) return wparam + 100;
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static DWORD WINAPI b_main( void *arg )
{
    MSG msg;

    (void)arg;
    b_window = CreateWindowExW( 0, L"WineNxMessagesB", L"b", WS_POPUP, 0, 0, 8, 8, NULL, NULL,
                                GetModuleHandleW( NULL ), NULL );
    SetEvent( b_ready );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        if (msg.message == WM_USER + 20) PostQuitMessage( 7 );
        DispatchMessageW( &msg );
    }
    DestroyWindow( b_window );
    return (DWORD)msg.wParam;
}

static void CALLBACK send_callback( HWND hwnd, UINT msg, ULONG_PTR data, LRESULT result )
{
    if (hwnd == b_window && msg == WM_USER + 6)
    {
        callback_data = data;
        callback_result = result;
    }
    InterlockedIncrement( &callback_calls );
}

/* Dispatch this thread's messages until *count reaches want or ms pass. */
static void pump( DWORD ms, const volatile LONG *count, LONG want )
{
    DWORD start = GetTickCount();
    MSG msg;

    for (;;)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        if (count && *count >= want) return;
        if (GetTickCount() - start >= ms) return;
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 10, QS_ALLINPUT );
    }
}

static DWORD test_timeout(void)
{
    DWORD_PTR result = 0xdead;

    SetLastError( 0 );
    if (SendMessageTimeoutW( b_window, WM_USER + 2, 0, 0, SMTO_NORMAL, 50, &result )) return 1;
    report( "SendMessageTimeout GetLastError (expected 5b4)", GetLastError() );
    if (GetLastError() != ERROR_TIMEOUT) return 2;
    Sleep( 400 );  /* let the slow handler finish */
    return SendMessageW( b_window, WM_USER + 1, 1, 0 ) == 2 ? 0 : 3;
}

static DWORD test_msg_wait(void)
{
    DWORD start, ret, elapsed;
    MSG msg;

    pump( 50, NULL, 0 );
    start = GetTickCount();
    ret = MsgWaitForMultipleObjects( 0, NULL, FALSE, 100, QS_ALLINPUT );
    elapsed = GetTickCount() - start;
    report( "empty queue wait result, ms", (ret << 16) | (elapsed & 0xffff) );
    if (ret != WAIT_TIMEOUT) return 1;
    if (elapsed < 80) return 2;
    /* Another thread posts to us after 50 ms. */
    if (!SendNotifyMessageW( b_window, WM_USER + 7, 0, 0 )) return 3;
    start = GetTickCount();
    ret = MsgWaitForMultipleObjects( 0, NULL, FALSE, 3000, QS_POSTMESSAGE );
    elapsed = GetTickCount() - start;
    report( "posted message wait result, ms", (ret << 16) | (elapsed & 0xffff) );
    if (ret != WAIT_OBJECT_0) return 4;
    if (!PeekMessageW( &msg, a_window, WM_USER + 11, WM_USER + 11, PM_REMOVE )) return 5;
    return 0;
}

static DWORD test_queue_status(void)
{
    DWORD status;
    MSG msg;

    pump( 20, NULL, 0 );
    if (!PostMessageW( a_window, WM_USER + 12, 0, 0 )) return 1;
    status = GetQueueStatus( QS_POSTMESSAGE );
    report( "GetQueueStatus after a post (expected 80008)", status );
    if (status != MAKELONG( QS_POSTMESSAGE, QS_POSTMESSAGE )) return 2;
    status = GetQueueStatus( QS_POSTMESSAGE );
    if (status != MAKELONG( 0, QS_POSTMESSAGE )) return 3;  /* still queued, no longer new */
    while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
    return GetQueueStatus( QS_POSTMESSAGE ) ? 4 : 0;
}

static DWORD test_clipboard(void)
{
    static const WCHAR name[] = L"Wine-NX test format";
    static const WCHAR hello[] = L"hello";
    WCHAR buffer[64];
    HGLOBAL mem;
    UINT format;
    WCHAR *wide;
    char *ansi;
    int i;

    format = RegisterClipboardFormatW( name );
    report( "registered clipboard format", format );
    if (format < 0xc000) return 1;
    if (RegisterWindowMessageW( name ) != format) return 2;
    if (GetClipboardFormatNameW( format, buffer, 64 ) != 19) return 3;
    for (i = 0; name[i]; i++) if (buffer[i] != name[i]) return 4;

    if (!OpenClipboard( a_window )) return 5;
    if (!EmptyClipboard()) return 6;
    if (!(mem = GlobalAlloc( GMEM_MOVEABLE, sizeof(hello) ))) return 7;
    wide = GlobalLock( mem );
    for (i = 0; i < 6; i++) wide[i] = hello[i];
    GlobalUnlock( mem );
    if (!SetClipboardData( CF_UNICODETEXT, mem )) return 8;
    if (!CloseClipboard()) return 9;

    if (!IsClipboardFormatAvailable( CF_TEXT )) return 10;
    if (CountClipboardFormats() < 2) return 11;
    if (GetClipboardOwner() != a_window) return 12;
    if (!OpenClipboard( a_window )) return 13;
    if (!EnumClipboardFormats( 0 )) return 14;
    if (!(mem = GetClipboardData( CF_TEXT )) || !(ansi = GlobalLock( mem ))) return 15;
    for (i = 0; i < 6; i++) if (ansi[i] != "hello"[i]) return 16;
    GlobalUnlock( mem );
    if (!(mem = GetClipboardData( CF_UNICODETEXT )) || !(wide = GlobalLock( mem ))) return 17;
    for (i = 0; i < 6; i++) if (wide[i] != hello[i]) return 18;
    GlobalUnlock( mem );
    return CloseClipboard() ? 0 : 19;
}

void __stdcall start(void)
{
    WNDCLASSW cls = {0};
    DWORD result, mask = 0, tid, code;
    HANDLE thread;

    cls.hInstance = GetModuleHandleW( NULL );
    cls.lpfnWndProc = b_proc;
    cls.lpszClassName = L"WineNxMessagesB";
    RegisterClassW( &cls );
    cls.lpfnWndProc = a_proc;
    cls.lpszClassName = L"WineNxMessagesA";
    RegisterClassW( &cls );
    a_window = CreateWindowExW( 0, L"WineNxMessagesA", L"a", WS_POPUP, 0, 0, 8, 8, NULL, NULL, cls.hInstance, NULL );
    b_ready = CreateEventW( NULL, TRUE, FALSE, NULL );
    thread = CreateThread( NULL, 0, b_main, NULL, 0, &tid );
    if (!a_window || !thread || WaitForSingleObject( b_ready, 10000 ) != WAIT_OBJECT_0 || !b_window)
    {
        report( "FAIL setup", GetLastError() );
        pe_test_terminate( 0x1ff );
    }

    report( "BEGIN SendMessage to another thread", 0 );
    result = SendMessageW( b_window, WM_USER + 1, 21, 0 ) == 42 ? 0 : 1;
    report( result ? "FAIL SendMessage" : "PASS SendMessage", result ); if (result) mask |= 1;

    report( "BEGIN SendMessageTimeout", 0 ); result = test_timeout();
    report( result ? "FAIL SendMessageTimeout" : "PASS SendMessageTimeout", result ); if (result) mask |= 2;

    report( "BEGIN SendNotifyMessage", 0 );
    result = SendNotifyMessageW( b_window, WM_USER + 3, 0, 0 ) ? 0 : 1;
    if (!result) { pump( 3000, &notify_count, 1 ); if (notify_count != 1) result = 2; }
    report( result ? "FAIL SendNotifyMessage" : "PASS SendNotifyMessage", result ); if (result) mask |= 4;

    report( "BEGIN nested send", 0 );
    result = SendMessageW( b_window, WM_USER + 4, 5, 0 );
    report( "nested result (expected 6a)", result );
    result = result == 106 ? 0 : 1;
    report( result ? "FAIL nested send" : "PASS nested send", result ); if (result) mask |= 8;

    report( "BEGIN ReplyMessage", 0 );
    result = SendMessageW( b_window, WM_USER + 5, 0, 0 ) == 77 ? 0 : 1;
    report( result ? "FAIL ReplyMessage" : "PASS ReplyMessage", result ); if (result) mask |= 16;

    report( "BEGIN SendMessageCallback", 0 );
    result = SendMessageCallbackW( b_window, WM_USER + 6, 0, 0, send_callback, 0x55 ) ? 0 : 1;
    if (!result)
    {
        pump( 3000, &callback_calls, 1 );
        if (callback_calls != 1) result = 2;
        else if (callback_result != 123 || callback_data != 0x55) result = 3;
    }
    report( result ? "FAIL SendMessageCallback" : "PASS SendMessageCallback", result ); if (result) mask |= 32;

    report( "BEGIN MsgWaitForMultipleObjects", 0 ); result = test_msg_wait();
    report( result ? "FAIL MsgWaitForMultipleObjects" : "PASS MsgWaitForMultipleObjects", result );
    if (result) mask |= 64;

    report( "BEGIN GetQueueStatus", 0 ); result = test_queue_status();
    report( result ? "FAIL GetQueueStatus" : "PASS GetQueueStatus", result ); if (result) mask |= 128;

    report( "BEGIN thread quit", 0 );
    result = PostThreadMessageW( tid, WM_USER + 20, 0, 0 ) ? 0 : 1;
    if (!result && WaitForSingleObject( thread, 10000 ) != WAIT_OBJECT_0) result = 2;
    if (!result && (!GetExitCodeThread( thread, &code ) || code != 7)) result = 3;
    report( result ? "FAIL thread quit" : "PASS thread quit", result ); if (result) mask |= 256;

    report( "BEGIN clipboard", 0 ); result = test_clipboard();
    report( result ? "FAIL clipboard" : "PASS clipboard", result ); if (result) mask |= 512;

    report( mask ? "FAIL combined mask" : "PASS ALL", mask );
    pe_test_terminate( mask ? 0x100 | mask : 42 );
    for (;;) {}
}
