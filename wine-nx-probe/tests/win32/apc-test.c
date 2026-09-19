/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * What Halo does to read the 2048-byte header of a map file, on its own:
 * ReadFileEx with a completion routine, then SleepEx(5000, TRUE) until the
 * routine sets a byte, giving up the moment the wait returns anything but
 * WAIT_IO_COMPLETION. Halo takes that path because the platform id says NT.
 *
 * Then what The Sims 2 Legacy does to a thread it starts: create it suspended,
 * queue it an APC and resume it. The APC runs before the thread's own code,
 * which must still start at its entry point with its parameter. And a window
 * procedure's result, which SendMessage returns after a callback.
 *
 * Last, faults in the program's own code, which must reach its handlers: The
 * Sims 2 reads address 0 in a __try to see that they do. A read and a write of
 * address 0 and a division by zero, each caught by a vectored handler that
 * sees the registers as the faulting instruction left them and steps past it.
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

static volatile LONG started_apc, started_entry;
static volatile ULONG_PTR started_param;

static void CALLBACK before_start( ULONG_PTR arg )
{
    started_apc = (LONG)arg;
}

static DWORD WINAPI started( void *param )
{
    started_param = (ULONG_PTR)param;
    started_entry = started_apc ? 2 : 1;   /* 2: the APC ran first */
    return 0x51;
}

static volatile LONG faults;
static DWORD fault_code[3], fault_kind[3], fault_address[3], fault_ebx[3];

static LONG CALLBACK catch_fault( EXCEPTION_POINTERS *ptrs )
{
    EXCEPTION_RECORD *rec = ptrs->ExceptionRecord;
    LONG n = faults;

    if (n >= 3 || (rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
                   rec->ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO))
        return EXCEPTION_CONTINUE_SEARCH;
    fault_code[n] = rec->ExceptionCode;
    fault_kind[n] = rec->NumberParameters >= 2 ? (DWORD)rec->ExceptionInformation[0] : 0xff;
    fault_address[n] = rec->NumberParameters >= 2 ? (DWORD)rec->ExceptionInformation[1] : 0xff;
    fault_ebx[n] = ptrs->ContextRecord->Ebx;
    /* Past the instruction: 5 bytes for the moves, 2 for the division. */
    ptrs->ContextRecord->Eip += rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ? 5 : 2;
    faults = n + 1;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static LRESULT CALLBACK answer_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_USER + 7) return 0x1234;
    return DefWindowProcA( hwnd, msg, wparam, lparam );
}

int WINAPI WinMain( HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show )
{
    char path[MAX_PATH], plain[192], overlapped[192], apc[96], thread[128], callback[64], seh[160], report[1280];
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

    /* A thread made suspended, given an APC, then resumed. */
    {
        HANDLE handle = CreateThread( NULL, 0, started, (void *)0x5eed, CREATE_SUSPENDED, NULL );
        DWORD code = 0;

        QueueUserAPC( before_start, handle, 7 );
        ResumeThread( handle );
        WaitForSingleObject( handle, 5000 );
        GetExitCodeThread( handle, &code );
        snprintf( thread, sizeof(thread), "apc=%ld entry=%ld param=%#lx exit=%#lx",
                  started_apc, started_entry, (unsigned long)started_param, code );
        CloseHandle( handle );
    }

    /* A window procedure's result through SendMessage. */
    {
        WNDCLASSA cls = { 0 };
        HWND hwnd;

        cls.lpfnWndProc = answer_proc;
        cls.hInstance = instance;
        cls.lpszClassName = "apc-test-answer";
        RegisterClassA( &cls );
        hwnd = CreateWindowA( "apc-test-answer", "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, instance, NULL );
        snprintf( callback, sizeof(callback), "SendMessage=%#lx",
                  hwnd ? (unsigned long)SendMessageA( hwnd, WM_USER + 7, 0, 0 ) : 0xdeadUL );
        if (hwnd) DestroyWindow( hwnd );
    }

    /* Faults in the program's own code, caught by its own handler. */
    {
        void *handler = AddVectoredExceptionHandler( 1, catch_fault );

        __asm__ __volatile__( "movl $0x1234, %%ebx\n\t"
                              ".byte 0xa0, 0, 0, 0, 0\n\t"      /* mov al, [0] */
                              "movl $0x5678, %%ebx\n\t"
                              ".byte 0xa2, 0, 0, 0, 0\n\t"      /* mov [0], al */
                              "movl $0x9abc, %%ebx\n\t"
                              "xorl %%edx, %%edx\n\t"
                              "xorl %%ecx, %%ecx\n\t"
                              "movl $1, %%eax\n\t"
                              "divl %%ecx\n\t"                   /* 2 bytes: f7 f1 */
                              ::: "eax", "ebx", "ecx", "edx", "memory" );
        RemoveVectoredExceptionHandler( handler );
        snprintf( seh, sizeof(seh), "faults=%ld read=%lx/%lu/%lx/%lx write=%lx/%lu/%lx/%lx div=%lx/%lx",
                  faults, fault_code[0], fault_kind[0], fault_address[0], fault_ebx[0],
                  fault_code[1], fault_kind[1], fault_address[1], fault_ebx[1], fault_code[2], fault_ebx[2] );
    }

    snprintf( report, sizeof(report),
              "platform=%lu | plain: %s | overlapped: %s | QueueUserAPC: %s | suspended thread: %s | %s | %s",
              version.dwPlatformId, plain, overlapped, apc, thread, callback, seh );

    if ((file = fopen( "apc-test.txt", "w" )))
    {
        fprintf( file, "%s\n", report );
        fclose( file );
    }
    /* "quiet" for a run on a desktop, where the box would wait for someone. */
    if (!strstr( cmdline, "quiet" )) MessageBoxA( NULL, report, "APC test", MB_OK );
    return 0;
}
