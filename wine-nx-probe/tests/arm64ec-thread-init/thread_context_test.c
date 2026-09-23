#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t DWORD64;

#define CONTEXT_AMD64 0x00100000
#define CONTEXT_AMD64_CONTROL (CONTEXT_AMD64 | 0x0001)
#define CONTEXT_AMD64_INTEGER (CONTEXT_AMD64 | 0x0002)
#define CONTEXT_AMD64_SEGMENTS (CONTEXT_AMD64 | 0x0004)
#define CONTEXT_AMD64_FLOATING_POINT (CONTEXT_AMD64 | 0x0008)
#define CONTEXT_AMD64_FULL \
    (CONTEXT_AMD64_CONTROL | CONTEXT_AMD64_INTEGER | CONTEXT_AMD64_FLOATING_POINT)

typedef struct
{
    DWORD64 homes[6];
    DWORD ContextFlags;
    DWORD AMD64_MxCsr_copy;
    WORD AMD64_SegCs;
    WORD AMD64_SegDs;
    WORD AMD64_SegEs;
    WORD AMD64_SegFs;
    WORD AMD64_SegGs;
    WORD AMD64_SegSs;
    DWORD AMD64_EFlags;
    BYTE reserved0[0x100 - 0x48];
    WORD AMD64_ControlWord;
    WORD AMD64_StatusWord;
    BYTE AMD64_TagWord;
    BYTE AMD64_Reserved1;
    WORD AMD64_ErrorOpcode;
    DWORD AMD64_ErrorOffset;
    WORD AMD64_ErrorSelector;
    WORD AMD64_Reserved2;
    DWORD AMD64_DataOffset;
    WORD AMD64_DataSelector;
    WORD AMD64_Reserved3;
    DWORD AMD64_MxCsr;
    DWORD AMD64_MxCsr_Mask;
    BYTE reserved1[0x4d0 - 0x120];
} ARM64EC_NT_CONTEXT;

_Static_assert( sizeof(ARM64EC_NT_CONTEXT) == 0x4d0, "context size" );
_Static_assert( offsetof(ARM64EC_NT_CONTEXT, ContextFlags) == 0x030, "flags offset" );
_Static_assert( offsetof(ARM64EC_NT_CONTEXT, AMD64_SegCs) == 0x038, "segments offset" );
_Static_assert( offsetof(ARM64EC_NT_CONTEXT, AMD64_ControlWord) == 0x100, "x87 offset" );
_Static_assert( offsetof(ARM64EC_NT_CONTEXT, AMD64_MxCsr) == 0x118, "mxcsr offset" );

#include "extracted_thread_context.inc"

static unsigned int failures;

static void report( const char *name, int passed )
{
    printf( "%s %s\n", passed ? "PASS" : "FAIL", name );
    failures += !passed;
}

static void test_fresh_context(void)
{
    ARM64EC_NT_CONTEXT context = {0}, expected = {0}, snapshot;

    expected.ContextFlags = CONTEXT_AMD64_FULL | CONTEXT_AMD64_SEGMENTS;
    expected.AMD64_SegCs = 0x33;
    expected.AMD64_SegDs = expected.AMD64_SegEs = expected.AMD64_SegGs =
        expected.AMD64_SegSs = 0x2b;
    expected.AMD64_SegFs = 0x53;
    expected.AMD64_EFlags = 0x202;
    expected.AMD64_MxCsr = expected.AMD64_MxCsr_copy = 0x1f80;
    expected.AMD64_MxCsr_Mask = 0xffff;
    expected.AMD64_ControlWord = 0x27f;

    initialize_thread_context( &context );
    report( "fresh-fex-baseline", !memcmp( &context, &expected, sizeof(context) ) );
    snapshot = context;
    initialize_thread_context( &context );
    report( "fresh-idempotent", !memcmp( &context, &snapshot, sizeof(context) ) );
}

static void test_supplied_context(void)
{
    ARM64EC_NT_CONTEXT context, snapshot;

    memset( &context, 0xa7, sizeof(context) );
    context.ContextFlags = CONTEXT_AMD64_CONTROL | CONTEXT_AMD64_INTEGER;
    context.AMD64_SegCs = 0x77;
    context.AMD64_ControlWord = 0x123;
    context.AMD64_MxCsr = 0x4567;
    snapshot = context;
    initialize_thread_context( &context );
    report( "supplied-context-preserved", !memcmp( &context, &snapshot, sizeof(context) ) );
}

int main(void)
{
    test_fresh_context();
    test_supplied_context();
    printf( "%u thread-context test failure(s)\n", failures );
    return failures != 0;
}
