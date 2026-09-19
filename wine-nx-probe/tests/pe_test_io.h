#ifndef WINE_NX_PE_TEST_IO_H
#define WINE_NX_PE_TEST_IO_H

#ifndef _WIN64
__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess( HANDLE process, NTSTATUS status );
#endif

static void pe_test_display_string( const UNICODE_STRING *str )
{
#ifdef _WIN64
    char text[512];
    DWORD chars = str->Length / sizeof(WCHAR), written;
    DWORD i;
    BOOL newline;

    newline = !chars || str->Buffer[chars - 1] != '\n';
    if (chars > sizeof(text) - newline) chars = sizeof(text) - newline;
    for (i = 0; i < chars; i++) text[i] = (char)str->Buffer[i];
    if (newline) text[chars++] = '\n';
    WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), text, chars, &written, NULL );
#else
    NtDisplayString( str );
#endif
}

static void pe_test_terminate( NTSTATUS status )
{
#ifdef _WIN64
    ExitProcess( status );
#else
    NtTerminateProcess( (HANDLE)-1, status );
#endif
    for (;;) Sleep( INFINITE );
}

#endif
