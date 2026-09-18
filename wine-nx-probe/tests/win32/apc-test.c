/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * What Halo does to read the 2048-byte header of a map file, on its own:
 * ReadFileEx with a completion routine, then SleepEx(5000, TRUE) until the
 * routine sets a byte, giving up the moment the wait returns anything but
 * WAIT_IO_COMPLETION. Halo takes that path because the platform id says NT.
 *
 * The result goes in a message box, which the runtime log records, and in
 * apc-test.txt beside the program.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static volatile LONG done_flag;
static DWORD done_error, done_bytes;
static char buffer[2048];

static void CALLBACK completed( DWORD error, DWORD bytes, LPOVERLAPPED overlapped )
{
    done_error = error;
    done_bytes = bytes;
    done_flag = 1;
}

static void CALLBACK queued( ULONG_PTR arg )
{
    done_flag = 1;
}

/* Halo's wait, byte for byte. Returns what SleepEx said last. */
static DWORD wait_for_it( int *rounds )
{
    DWORD ret = 0;

    for (*rounds = 0; !done_flag && *rounds < 3; (*rounds)++)
    {
        ret = SleepEx( 5000, TRUE );
        if (ret != WAIT_IO_COMPLETION) break;
    }
    return ret;
}

static int read_it( const char *path, DWORD flags, char *out, size_t size )
{
    OVERLAPPED overlapped;
    DWORD sleep_ret, header = 0;
    int rounds = 0;
    BOOL ok;
    HANDLE file = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, flags, NULL );

    if (file == INVALID_HANDLE_VALUE)
    {
        snprintf( out, size, "open=%lu", GetLastError() );
        return 0;
    }
    done_flag = 0;
    done_error = 0xdead;
    done_bytes = 0xdead;
    memset( buffer, 0, sizeof(buffer) );
    memset( &overlapped, 0, sizeof(overlapped) );
    SetLastError( 0 );
    ok = ReadFileEx( file, buffer, sizeof(buffer), &overlapped, completed );
    sleep_ret = wait_for_it( &rounds );
    memcpy( &header, buffer, sizeof(header) );
    snprintf( out, size, "rfe=%d gle=%lu sleep=%lx rounds=%d flag=%ld err=%lu n=%lu head=%08lx",
              ok ? 1 : 0, GetLastError(), sleep_ret, rounds, done_flag, done_error, done_bytes,
              (unsigned long)header );
    CloseHandle( file );
    return ok && done_flag && done_bytes == sizeof(buffer);
}

int WINAPI WinMain( HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show )
{
    char path[MAX_PATH], plain[192], overlapped[192], apc[96], report[640];
    OSVERSIONINFOA version = { sizeof(version) };
    DWORD sleep_ret;
    int rounds = 0;
    FILE *file;

    GetModuleFileNameA( NULL, path, sizeof(path) );
    GetVersionExA( &version );

    read_it( path, 0, plain, sizeof(plain) );
    read_it( path, FILE_FLAG_OVERLAPPED, overlapped, sizeof(overlapped) );

    /* The same delivery without any file behind it. */
    done_flag = 0;
    QueueUserAPC( queued, GetCurrentThread(), 0 );
    sleep_ret = wait_for_it( &rounds );
    snprintf( apc, sizeof(apc), "sleep=%lx rounds=%d flag=%ld", sleep_ret, rounds, done_flag );

    snprintf( report, sizeof(report), "platform=%lu | plain: %s | overlapped: %s | QueueUserAPC: %s",
              version.dwPlatformId, plain, overlapped, apc );

    if ((file = fopen( "apc-test.txt", "w" )))
    {
        fprintf( file, "%s\n", report );
        fclose( file );
    }
    MessageBoxA( NULL, report, "APC test", MB_OK );
    return 0;
}
