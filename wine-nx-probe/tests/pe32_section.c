/* Wine-NX section checkpoint: sections with no file behind them
 * (CreateFileMapping with INVALID_HANDLE_VALUE), where DXVK's 32-bit d3d9 keeps
 * its system-memory resources. Every view of a section shows the same memory,
 * so this checks that views of overlapping parts see each other's writes at
 * once, that data survives unmapping and mapping again, that a page made
 * inaccessible in one view comes back with what another view wrote meanwhile,
 * that views keep the memory after the section handle is closed, that a named
 * section opens again with its data, that a SEC_RESERVE section's committed
 * pages are tracked for all its views, and that a copy view keeps its writes
 * to itself. It also times the map, write and unmap cycle DXVK repeats. Exit
 * 42 means every step passed. */
#include <windows.h>
#include <winternl.h>

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess( HANDLE process, NTSTATUS status );

#define WINDOW 0x10000
#define SECTION_SIZE (16 << 20)

/* Built without a C runtime; the compiler still expects these for structures. */
void *memset( void *dst, int c, size_t n )
{
    unsigned char *p = dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

void *memcpy( void *dst, const void *src, size_t n )
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

static void report_text( const char *label, const char *text )
{
    const char *prefix = "[SECTION TEST] ";
    WCHAR buffer[320];
    UNICODE_STRING str;
    unsigned int n = 0;

    while (*prefix) buffer[n++] = *prefix++;
    while (*label && n < 200) buffer[n++] = *label++;
    if (text)
    {
        buffer[n++] = ' ';
        while (*text && n < 316) buffer[n++] = (unsigned char)*text++;
    }
    buffer[n++] = '\n';
    str.Buffer = buffer;
    str.Length = n * sizeof(WCHAR);
    str.MaximumLength = str.Length;
    NtDisplayString( &str );
}

static void report( const char *label, DWORD value )
{
    static const char hex[] = "0123456789abcdef";
    char text[11];
    unsigned int i;

    text[0] = '0';
    text[1] = 'x';
    for (i = 0; i < 8; i++) text[2 + i] = hex[(value >> (28 - i * 4)) & 15];
    text[10] = 0;
    report_text( label, text );
}

static unsigned char pattern( DWORD offset, unsigned char seed )
{
    return (unsigned char)(offset * 131 + seed);
}

static void fill( unsigned char *ptr, DWORD offset, DWORD size, unsigned char seed )
{
    DWORD i;

    for (i = 0; i < size; i++) ptr[i] = pattern( offset + i, seed );
}

static BOOL holds( const unsigned char *ptr, DWORD offset, DWORD size, unsigned char seed )
{
    DWORD i;

    for (i = 0; i < size; i++) if (ptr[i] != pattern( offset + i, seed )) return FALSE;
    return TRUE;
}

static BOOL zeroed( const unsigned char *ptr, DWORD size )
{
    DWORD i;

    for (i = 0; i < size; i++) if (ptr[i]) return FALSE;
    return TRUE;
}

/* The size VirtualQuery gives the region at addr, reported with its state. */
static SIZE_T region_size( const char *label, void *addr )
{
    MEMORY_BASIC_INFORMATION info;

    if (!VirtualQuery( addr, &info, sizeof(info) )) return 0;
    report( label, (DWORD)info.RegionSize );
    return info.RegionSize;
}

void __stdcall start(void)
{
    static const WCHAR name[] = L"wine-nx-section-test";
    HANDLE section = NULL, named = NULL, again = NULL, opened = NULL, reserved = NULL, copied = NULL;
    unsigned char *a, *b, *c, *d, *whole, *view, *second, *copy;
    DWORD failure = 0, begin, error, old, i;

    report( "BEGIN", 0 );
    section = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0, SECTION_SIZE, NULL );
    report( "CreateFileMappingW error", section ? 0 : GetLastError() );
    if (!section) { failure = 1; goto done; }

    /* Views of two different parts, as DXVK maps a 64 KiB window of a section
     * per resource. */
    a = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 5 * WINDOW, WINDOW );
    b = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 9 * WINDOW, 3 * WINDOW );
    if (!a || !b)
    {
        report( "MapViewOfFile error", GetLastError() );
        failure = 2;
        goto done;
    }
    if (!zeroed( a, WINDOW ) || !zeroed( b, 3 * WINDOW )) { failure = 3; goto done; }
    fill( a, 5 * WINDOW, WINDOW, 1 );
    fill( b, 9 * WINDOW, 3 * WINDOW, 2 );
    if (!UnmapViewOfFile( a ) || !UnmapViewOfFile( b )) { failure = 4; goto done; }

    a = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 5 * WINDOW, WINDOW );
    if (!a || !holds( a, 5 * WINDOW, WINDOW, 1 )) { failure = 5; goto done; }
    UnmapViewOfFile( a );
    if (!(whole = MapViewOfFile( section, FILE_MAP_READ, 0, 0, 0 )))
    {
        report( "whole MapViewOfFile error", GetLastError() );
        failure = 6;
        goto done;
    }
    if (!zeroed( whole, 5 * WINDOW ) || !holds( whole + 5 * WINDOW, 5 * WINDOW, WINDOW, 1 ) ||
        !zeroed( whole + 6 * WINDOW, 3 * WINDOW ) || !holds( whole + 9 * WINDOW, 9 * WINDOW, 3 * WINDOW, 2 ) ||
        !zeroed( whole + 12 * WINDOW, SECTION_SIZE - 12 * WINDOW ))
    {
        failure = 7;
        goto done;
    }
    UnmapViewOfFile( whole );
    report_text( "unmapped views kept their data", NULL );

    /* The cycle DXVK repeats for each locked resource. */
    begin = GetTickCount();
    for (i = 0; i < 200; i++)
    {
        if (!(view = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, (i % 64) * WINDOW, WINDOW )))
        {
            report( "cycle", i );
            failure = 8;
            goto done;
        }
        view[i] = (unsigned char)i;
        UnmapViewOfFile( view );
    }
    report( "200 map/write/unmap cycles, milliseconds", GetTickCount() - begin );

    /* Views of overlapping parts: [0, 3 windows) and [2 windows, 5 windows). */
    a = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 0, 3 * WINDOW );
    b = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 2 * WINDOW, 3 * WINDOW );
    if (!a || !b)
    {
        report( "overlapping MapViewOfFile error", GetLastError() );
        failure = 9;
        goto done;
    }
    a[2 * WINDOW + 7] = 0x5a;
    b[WINDOW - 1] = 0xa5;
    if (b[7] != 0x5a || a[3 * WINDOW - 1] != 0xa5) { failure = 10; goto done; }
    report_text( "overlapping views saw each other's writes", NULL );

    /* The shared window goes inaccessible in a while b writes to it. */
    if (!VirtualProtect( a + 2 * WINDOW, WINDOW, PAGE_NOACCESS, &old ) || old != PAGE_READWRITE)
    {
        report( "VirtualProtect PAGE_NOACCESS error", GetLastError() );
        failure = 11;
        goto done;
    }
    b[9] = 0x99;
    if (!VirtualProtect( a + 2 * WINDOW, WINDOW, PAGE_READWRITE, &old ) || old != PAGE_NOACCESS)
    {
        report( "VirtualProtect PAGE_READWRITE error", GetLastError() );
        failure = 12;
        goto done;
    }
    if (a[2 * WINDOW + 9] != 0x99 || a[2 * WINDOW + 7] != 0x5a) { failure = 13; goto done; }
    a[2 * WINDOW + 10] = 0x10;
    if (b[10] != 0x10) { failure = 14; goto done; }
    UnmapViewOfFile( a );
    UnmapViewOfFile( b );
    report_text( "an inaccessible window came back with the other view's writes", NULL );

    /* Views keep the memory after the section's handle is closed. */
    c = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 0, 2 * WINDOW );
    d = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, WINDOW, 2 * WINDOW );
    CloseHandle( section );
    section = NULL;
    if (!c || !d) { failure = 15; goto done; }
    c[WINDOW + 5] = 0x55;
    if (d[5] != 0x55) { failure = 16; goto done; }
    if (!UnmapViewOfFile( c )) { failure = 17; goto done; }
    d[6] = 0x66;
    if (d[5] != 0x55 || d[6] != 0x66) { failure = 18; goto done; }
    if (!UnmapViewOfFile( d )) { failure = 19; goto done; }
    report_text( "views kept the memory after the handle was closed", NULL );

    /* A named section opens again with what was written to it. */
    named = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, WINDOW, name );
    if (!named)
    {
        report( "named CreateFileMappingW error", GetLastError() );
        failure = 20;
        goto done;
    }
    if (!(view = MapViewOfFile( named, FILE_MAP_ALL_ACCESS, 0, 0, 0 ))) { failure = 21; goto done; }
    fill( view, 0, WINDOW, 4 );
    UnmapViewOfFile( view );
    again = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, WINDOW, name );
    error = GetLastError();
    if (!again || error != ERROR_ALREADY_EXISTS)
    {
        report( "same name again, GetLastError", error );
        failure = 22;
        goto done;
    }
    if (!(opened = OpenFileMappingW( FILE_MAP_ALL_ACCESS, FALSE, name )))
    {
        report( "OpenFileMappingW error", GetLastError() );
        failure = 23;
        goto done;
    }
    if (!(view = MapViewOfFile( opened, FILE_MAP_READ, 0, 0, 0 )) || !holds( view, 0, WINDOW, 4 ))
    {
        failure = 24;
        goto done;
    }
    UnmapViewOfFile( view );
    report_text( "a named section opened again with its data", NULL );

    /* SEC_RESERVE: committing a window in one view is seen through another.
     * Wine keeps every page of a view accessible and sizes VirtualQuery
     * regions by the committed ranges the server tracks. */
    reserved = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_RESERVE, 0, 4 * WINDOW, NULL );
    if (!reserved || !(view = MapViewOfFile( reserved, FILE_MAP_ALL_ACCESS, 0, 0, 0 )))
    {
        report( "SEC_RESERVE section error", GetLastError() );
        failure = 25;
        goto done;
    }
    if (region_size( "reserved view region", view ) != 4 * WINDOW) { failure = 26; goto done; }
    if (!VirtualAlloc( view + WINDOW, WINDOW, MEM_COMMIT, PAGE_READWRITE ))
    {
        report( "VirtualAlloc MEM_COMMIT error", GetLastError() );
        failure = 27;
        goto done;
    }
    if (region_size( "region before the committed window", view ) != WINDOW ||
        region_size( "committed window region", view + WINDOW ) != WINDOW ||
        region_size( "region after the committed window", view + 2 * WINDOW ) != 2 * WINDOW)
    {
        failure = 28;
        goto done;
    }
    view[WINDOW + 3] = 0x33;
    if (!(second = MapViewOfFile( reserved, FILE_MAP_ALL_ACCESS, 0, 0, 0 ))) { failure = 29; goto done; }
    if (region_size( "committed window region in a second view", second + WINDOW ) != WINDOW ||
        second[WINDOW + 3] != 0x33)
    {
        failure = 30;
        goto done;
    }
    UnmapViewOfFile( second );
    UnmapViewOfFile( view );
    report_text( "a committed window was tracked for both views", NULL );

    /* A copy view starts with the section's data and keeps its writes. */
    copied = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, WINDOW, NULL );
    if (!copied || !(view = MapViewOfFile( copied, FILE_MAP_ALL_ACCESS, 0, 0, 0 ))) { failure = 31; goto done; }
    view[0] = 1;
    if (!(copy = MapViewOfFile( copied, FILE_MAP_COPY, 0, 0, 0 )))
    {
        report( "FILE_MAP_COPY error", GetLastError() );
        failure = 32;
        goto done;
    }
    if (copy[0] != 1) { failure = 33; goto done; }
    copy[0] = 2;
    if (view[0] != 1) { failure = 34; goto done; }
    UnmapViewOfFile( copy );
    UnmapViewOfFile( view );
    report_text( "a copy view kept its writes to itself", NULL );

done:
    if (copied) CloseHandle( copied );
    if (reserved) CloseHandle( reserved );
    if (opened) CloseHandle( opened );
    if (again) CloseHandle( again );
    if (named) CloseHandle( named );
    if (section) CloseHandle( section );
    if (failure) report( "FAIL step", failure );
    else report_text( "PASS", NULL );
    NtTerminateProcess( GetCurrentProcess(), failure ? failure : 42 );
}
