/* Reproduce OpenTTD's first Win32 GDI startup calls with an exact breadcrumb
 * around every operation.  No CRT is used, so this remains useful when the
 * failure is in process startup or exception handling.  Exit 42 means pass. */
#include <windows.h>
#include <winternl.h>

#include "pe_test_io.h"

static void report( const WCHAR *message, ULONG_PTR value )
{
    static const WCHAR prefix[] = L"[VIDEO TEST] ";
    static const WCHAR hex[] = L"0123456789abcdef";
    WCHAR text[128];
    UNICODE_STRING str;
    unsigned int i, n = 0;

    for (i = 0; prefix[i]; i++) text[n++] = prefix[i];
    for (i = 0; message[i] && n < 98; i++) text[n++] = message[i];
    text[n++] = L' '; text[n++] = L'0'; text[n++] = L'x';
    for (i = 0; i < sizeof(value) * 2; i++)
        text[n++] = hex[(value >> ((sizeof(value) * 2 - 1 - i) * 4)) & 15];
    text[n++] = L'\n';
    text[n] = 0;
    str.Buffer = text;
    str.Length = n * sizeof(WCHAR);
    str.MaximumLength = (n + 1) * sizeof(WCHAR);
    pe_test_display_string( &str );
}

static LRESULT WINAPI test_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_CREATE) report( L"WndProc WM_CREATE", (ULONG_PTR)hwnd );
    if (msg == WM_DESTROY) report( L"WndProc WM_DESTROY", (ULONG_PTR)hwnd );
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

void __stdcall start(void)
{
    WNDCLASSW cls = {0};
    HINSTANCE instance;
    HICON icon;
    HCURSOR cursor;
    ATOM atom;
    HWND window;
    DWORD error;

    report( L"BEGIN", 0 );
    report( L"before GetModuleHandleW", 0 );
    instance = GetModuleHandleW( NULL );
    report( L"after GetModuleHandleW", (ULONG_PTR)instance );

    /* OpenTTD uses LoadIcon(hinst, MAKEINTRESOURCE(100)).  Resource 100 is
     * embedded in this executable so this follows that same user32 path. */
    report( L"before application LoadIconW", 100 );
    icon = LoadIconW( instance, MAKEINTRESOURCEW(100) );
    report( L"after application LoadIconW", (ULONG_PTR)icon );
    if (!icon)
    {
        error = GetLastError();
        report( L"application LoadIconW error", error );
        pe_test_terminate( 0x201 );
    }

    report( L"before system LoadCursorW", 32512 );
    cursor = LoadCursorW( NULL, MAKEINTRESOURCEW(32512) );
    report( L"after system LoadCursorW", (ULONG_PTR)cursor );
    if (!cursor)
    {
        error = GetLastError();
        report( L"system LoadCursorW error", error );
        pe_test_terminate( 0x202 );
    }

    cls.style = CS_OWNDC;
    cls.lpfnWndProc = test_proc;
    cls.hInstance = instance;
    cls.hIcon = icon;
    cls.hCursor = cursor;
    cls.lpszClassName = L"WineNxVideoStartup";
    report( L"before RegisterClassW", 0 );
    atom = RegisterClassW( &cls );
    report( L"after RegisterClassW", atom );
    if (!atom)
    {
        error = GetLastError();
        report( L"RegisterClassW error", error );
        pe_test_terminate( 0x203 );
    }

    report( L"before CreateWindowExW", 0 );
    window = CreateWindowExW( 0, cls.lpszClassName, L"Wine-NX video startup test",
                              WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, NULL, NULL,
                              instance, NULL );
    report( L"after CreateWindowExW", (ULONG_PTR)window );
    if (!window)
    {
        error = GetLastError();
        report( L"CreateWindowExW error", error );
        pe_test_terminate( 0x204 );
    }

    report( L"before ShowWindow", 0 );
    ShowWindow( window, SW_SHOW );
    UpdateWindow( window );
    report( L"after ShowWindow/UpdateWindow", 0 );
    DestroyWindow( window );
    UnregisterClassW( cls.lpszClassName, instance );
    report( L"PASS ALL", 42 );
    pe_test_terminate( 42 );
    for (;;) {}
}
