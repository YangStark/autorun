/* Wine-NX PE integration test. No CRT; real kernel32/ntdll imports.
 * TLS coverage is single-thread allocation, values, expansion and reuse.
 * Exit 42 means all groups passed; 0x100 | mask reports failing groups:
 * 1=time, 2=heap, 4=TLS, 8=file I/O. Detail codes are printed before exit. */
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

static DWORD test_time(void)
{
    LARGE_INTEGER time;
    NTSTATUS status = NtQuerySystemTime( &time );
    return status ? (DWORD)status : time.QuadPart ? 0 : 1;
}

static DWORD test_heap(void)
{
    HANDLE heap = GetProcessHeap(), private_heap;
    unsigned char *p, *grown;
    DWORD result = 0, i;
    if (!heap) return 1;
    p = HeapAlloc( heap, HEAP_ZERO_MEMORY, 257 );
    if (!p) return 2;
    for (i = 0; i < 257; i++) if (p[i]) { result = 3; goto done; }
    for (i = 0; i < 257; i++) p[i] = (unsigned char)(i * 29 + 7);
    grown = HeapReAlloc( heap, HEAP_ZERO_MEMORY, p, 8193 );
    if (!grown) { result = 4; goto done; }
    p = grown;
    for (i = 0; i < 257; i++) if (p[i] != (unsigned char)(i * 29 + 7)) { result = 5; goto done; }
    for (i = 257; i < 8193; i++) if (p[i]) { result = 6; goto done; }
    if (HeapSize( heap, 0, p ) < 8193 || HeapSize( heap, 0, p ) == (SIZE_T)-1) result = 7;
done:
    if (!HeapFree( heap, 0, p ) && !result) result = 8;
    if (result) return result;
    private_heap = HeapCreate( 0, 0, 0 );
    if (!private_heap) return 9;
    p = HeapAlloc( private_heap, 0, 4097 );
    if (!p) result = 10;
    else
    {
        p[0] = 0x12; p[4096] = 0x34;
        if (p[0] != 0x12 || p[4096] != 0x34) result = 11;
        if (!HeapFree( private_heap, 0, p ) && !result) result = 12;
    }
    if (!HeapDestroy( private_heap ) && !result) result = 13;
    return result;
}

static DWORD test_tls(void)
{
    DWORD slots[80], count = 0, result = 0, i, j, slot;
    BOOL expansion = FALSE;
    for (i = 0; i < 80; i++)
    {
        slot = TlsAlloc();
        if (slot == TLS_OUT_OF_INDEXES) { result = 1; break; }
        slots[count++] = slot;
        if (slot >= TLS_MINIMUM_AVAILABLE) expansion = TRUE;
        for (j = 0; j < i; j++) if (slots[j] == slot) result = 2;
        if (result) break;
        SetLastError( 0x1234 );
        if (TlsGetValue( slot ) || GetLastError()) { result = 3; break; }
        if (!TlsSetValue( slot, (void *)(ULONG_PTR)(0x10000 + i * 16))) { result = 4; break; }
    }
    if (!result && !expansion) result = 5;
    for (i = 0; !result && i < count; i++)
        if (TlsGetValue( slots[i] ) != (void *)(ULONG_PTR)(0x10000 + i * 16)) result = 6;
    for (i = 0; i < count; i++) if (!TlsFree( slots[i] ) && !result) result = 7;
    if (result) return result;
    slot = TlsAlloc();
    if (slot == TLS_OUT_OF_INDEXES) return 8;
    if (TlsGetValue( slot )) result = 9;
    if (!TlsFree( slot ) && !result) result = 10;
    return result;
}

static DWORD test_file(void)
{
    const WCHAR *path = L"C:\\wine-nx-functional-test.tmp";
    unsigned char data[513], readback[513];
    HANDLE file;
    DWORD count, i, size, result = 0;
    for (i = 0; i < sizeof(data); i++) data[i] = (unsigned char)(i * 37 + 11);
    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) { report( "file create GetLastError", GetLastError() ); return 1; }
    if (!WriteFile( file, data, sizeof(data), &count, NULL ) || count != sizeof(data)) result = 2;
    if (!result && !FlushFileBuffers( file )) result = 3;
    if (!CloseHandle( file ) && !result) result = 4;
    if (result) goto cleanup;
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) { result = 5; goto cleanup; }
    size = GetFileSize( file, NULL );
    if (size != sizeof(data)) result = 6;
    if (!result && (!ReadFile( file, readback, sizeof(readback), &count, NULL ) || count != sizeof(data))) result = 7;
    for (i = 0; !result && i < sizeof(data); i++) if (data[i] != readback[i]) result = 8;
    if (!result && (!ReadFile( file, readback, 1, &count, NULL ) || count != 0)) result = 9;
    if (!result && SetFilePointer( file, 127, NULL, FILE_BEGIN ) != 127) result = 10;
    if (!result && (!ReadFile( file, readback, 17, &count, NULL ) || count != 17)) result = 11;
    for (i = 0; !result && i < 17; i++) if (readback[i] != data[127 + i]) result = 12;
    if (!CloseHandle( file ) && !result) result = 13;
cleanup:
    if (!DeleteFileW( path ) && !result) result = 14;
    if (!result && GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES) result = 15;
    return result;
}

void __stdcall start(void)
{
    DWORD result, mask = 0;
    report( "BEGIN time", 0 ); result = test_time();
    report( result ? "FAIL time" : "PASS time", result ); if (result) mask |= 1;
    report( "BEGIN heap", 0 ); result = test_heap();
    report( result ? "FAIL heap" : "PASS heap", result ); if (result) mask |= 2;
    report( "BEGIN TLS (single thread, 80 slots)", 0 ); result = test_tls();
    report( result ? "FAIL TLS" : "PASS TLS", result ); if (result) mask |= 4;
    report( "BEGIN file I/O", 0 ); result = test_file();
    report( result ? "FAIL file I/O" : "PASS file I/O", result ); if (result) mask |= 8;
    report( mask ? "FAIL combined mask" : "PASS ALL", mask );
    pe_test_terminate( mask ? 0x100 | mask : 42 );
    for (;;) {}
}
