#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <emmintrin.h>
#include <stdint.h>
#include <stdlib.h>

static HANDLE log_file;
static LONG failures;
static LONG callback_count;
static __thread volatile LONG tls_value = 0x13579;
static volatile int sse_input[4] = {10, 20, 30, 40};
static volatile float rounding_input = 1.75f;

static DWORD string_length( const char *str )
{
    const char *end = str;
    while (*end) ++end;
    return (DWORD)(end - str);
}

static void write_text( const char *text )
{
    DWORD written;
    HANDLE output = GetStdHandle( STD_OUTPUT_HANDLE );
    DWORD length = string_length( text );

    if (log_file != INVALID_HANDLE_VALUE) WriteFile( log_file, text, length, &written, NULL );
    if (output && output != INVALID_HANDLE_VALUE) WriteFile( output, text, length, &written, NULL );
}

static void write_hex64( ULONGLONG value )
{
    static const char digits[] = "0123456789abcdef";
    char buffer[19];
    unsigned int i;

    buffer[0] = '0';
    buffer[1] = 'x';
    for (i = 0; i < 16; ++i) buffer[2 + i] = digits[(value >> (60 - i * 4)) & 15];
    buffer[18] = 0;
    write_text( buffer );
}

static void result( const char *name, BOOL passed )
{
    write_text( passed ? "PASS " : "FAIL " );
    write_text( name );
    write_text( "\r\n" );
    if (!passed) InterlockedIncrement( &failures );
}

static int __cdecl compare_ints( const void *left, const void *right )
{
    int a = *(const int *)left, b = *(const int *)right;
    InterlockedIncrement( &callback_count );
    return (a > b) - (a < b);
}

static DWORD WINAPI thread_main( void *argument )
{
    LONG *observed = argument;

    *observed = tls_value;
    tls_value = 0x2468a;
    return tls_value == 0x2468a ? 0x64ec : 1;
}

static void *allocate_high(void)
{
    static const ULONG_PTR hints[] =
    {
        0x0000000100000000ull,
        0x0000000140000000ull,
        0x0000001000000000ull,
        0x0000010000000000ull
    };
    unsigned int i;

    for (i = 0; i < sizeof(hints) / sizeof(hints[0]); ++i)
    {
        void *ptr = VirtualAlloc( (void *)hints[i], 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
        if (ptr) return ptr;
    }
    return VirtualAlloc( NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
}

int main(void)
{
    int values[] = { 19, -7, 4, 4, 0, 81, -12 };
    const int sorted[] = { -12, -7, 0, 4, 4, 19, 81 };
    int lanes[4];
    __m128i a, b, c;
    unsigned int saved_mxcsr, mode;
    static const int rounded[] = {2, 1, 2, 1};
    ULONG_PTR teb, peb, stack_base, stack_limit, local;
    void *high;
    HANDLE thread;
    DWORD thread_id, exit_code = 0;
    LONG thread_tls = 0;
    BOOL ok;

    log_file = CreateFileA( "pe64_smoke.log", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    write_text( "Wine-NX AMD64 PE smoke\r\n" );

    result( "win64-pointer", sizeof(void *) == 8 );
    result( "kernel32-imports", GetModuleHandleW( L"kernel32.dll" ) != NULL &&
                                  GetCurrentProcessId() != 0 );

    teb = (ULONG_PTR)__readgsqword( 0x30 );
    peb = teb ? *(ULONG_PTR *)(teb + 0x60) : 0;
    stack_base = teb ? *(ULONG_PTR *)(teb + 0x08) : 0;
    stack_limit = teb ? *(ULONG_PTR *)(teb + 0x10) : 0;
    local = (ULONG_PTR)&teb;
    result( "gs-teb-peb", teb != 0 && peb != 0 && teb == (ULONG_PTR)NtCurrentTeb() );
    write_text( "INFO teb=" );
    write_hex64( teb );
    write_text( " peb=" );
    write_hex64( peb );
    write_text( "\r\n" );
    result( "teb-stack-bounds", stack_limit < local && local < stack_base );

    high = allocate_high();
    ok = (ULONG_PTR)high > 0xffffffffull;
    if (high)
    {
        *(volatile ULONGLONG *)high = 0x1122334455667788ull;
        ok = ok && *(volatile ULONGLONG *)high == 0x1122334455667788ull;
    }
    result( "virtualalloc-above-4g", ok );
    write_text( "INFO allocation=" );
    write_hex64( (ULONG_PTR)high );
    write_text( "\r\n" );
    if (high) VirtualFree( high, 0, MEM_RELEASE );

    a = _mm_set_epi32( sse_input[3], sse_input[2], sse_input[1], sse_input[0] );
    b = _mm_set1_epi32( 2 );
    c = _mm_add_epi32( a, b );
    _mm_storeu_si128( (__m128i *)lanes, c );
    result( "sse2", IsProcessorFeaturePresent( PF_XMMI64_INSTRUCTIONS_AVAILABLE ) &&
                     lanes[0] == 12 && lanes[1] == 22 && lanes[2] == 32 && lanes[3] == 42 );

    saved_mxcsr = _mm_getcsr();
    ok = TRUE;
    for (mode = 0; mode < 4; ++mode)
    {
        _mm_setcsr( (saved_mxcsr & ~0x6000u) | (mode << 13) );
        ok = ok && GetCurrentThreadId() != 0;
        ok = ok && (_mm_getcsr() & 0x6000) == (mode << 13);
        ok = ok && _mm_cvtss_si32( _mm_set_ss( rounding_input ) ) == rounded[mode];
    }
    _mm_setcsr( saved_mxcsr );
    result( "mxcsr-native-call", ok );

    qsort( values, sizeof(values) / sizeof(values[0]), sizeof(values[0]), compare_ints );
    ok = callback_count > 0;
    for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        ok = ok && values[i] == sorted[i];
    result( "qsort-callback", ok );

    result( "tls-main-initial", tls_value == 0x13579 );
    thread = CreateThread( NULL, 0, thread_main, &thread_tls, 0, &thread_id );
    ok = thread != NULL;
    if (thread)
    {
        ok = WaitForSingleObject( thread, 10000 ) == WAIT_OBJECT_0;
        ok = ok && GetExitCodeThread( thread, &exit_code ) && exit_code == 0x64ec;
        CloseHandle( thread );
    }
    result( "create-thread-join", ok );
    result( "tls-per-thread", thread_tls == 0x13579 && tls_value == 0x13579 );

    write_text( failures ? "RESULT FAIL\r\n" : "RESULT PASS\r\n" );
    if (log_file != INVALID_HANDLE_VALUE) CloseHandle( log_file );
    return failures > 255 ? 255 : failures;
}
