#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int32_t NTSTATUS;
typedef uint16_t WCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef uint32_t ACCESS_MASK;
typedef void *HANDLE;

typedef struct
{
    USHORT Length;
    USHORT MaximumLength;
    WCHAR *Buffer;
} UNICODE_STRING;

typedef struct
{
    ULONG Length;
    HANDLE RootDirectory;
    UNICODE_STRING *ObjectName;
    ULONG Attributes;
    void *SecurityDescriptor;
    void *SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xc0000022)
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xc0000034)
#define STATUS_OBJECT_PATH_SYNTAX_BAD ((NTSTATUS)0xc000003b)
#define STATUS_NAME_TOO_LONG ((NTSTATUS)0xc0000106)
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define OBJ_CASE_INSENSITIVE 0x0040
#define REG_OPTION_VOLATILE 0x0001
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define InitializeObjectAttributes(p,n,a,r,s) do { \
    (p)->Length = sizeof(*(p)); \
    (p)->RootDirectory = (r); \
    (p)->Attributes = (a); \
    (p)->ObjectName = (n); \
    (p)->SecurityDescriptor = (s); \
    (p)->SecurityQualityOfService = NULL; \
} while (0)

static const char *const paths[] =
{
    "\\Registry",
    "\\Registry\\Machine",
    "\\Registry\\Machine\\Software",
    "\\Registry\\Machine\\Software\\Microsoft",
    "\\Registry\\Machine\\Software\\Microsoft\\Wow64",
    "\\Registry\\Machine\\Software\\Microsoft\\Wow64\\amd64"
};
static unsigned int calls, closes, existing = 3, fail_call;
static unsigned int failures;

static int unicode_equals_ascii( const UNICODE_STRING *name, const char *ascii )
{
    size_t i, length = strlen( ascii );

    if (name->Length != length * sizeof(WCHAR)) return 0;
    for (i = 0; i < length; i++) if (name->Buffer[i] != (unsigned char)ascii[i]) return 0;
    return 1;
}

static NTSTATUS NtCreateKey( HANDLE *key, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                             ULONG index, const UNICODE_STRING *class, ULONG options, ULONG *dispos )
{
    unsigned int current = calls++;

    (void)index;
    (void)class;
    (void)dispos;
    *key = NULL;
    if (fail_call && calls == fail_call) return STATUS_ACCESS_DENIED;
    if (current >= ARRAY_SIZE(paths) || !unicode_equals_ascii( attr->ObjectName, paths[current] ))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    if (current > existing) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (current == existing) existing++;
    if (access != (KEY_CREATE_SUB_KEY | KEY_SET_VALUE) || options != REG_OPTION_VOLATILE ||
        attr->Attributes != OBJ_CASE_INSENSITIVE || attr->RootDirectory)
        return STATUS_ACCESS_DENIED;
    *key = (HANDLE)(uintptr_t)(current + 1);
    return STATUS_SUCCESS;
}

static NTSTATUS NtClose( HANDLE handle )
{
    (void)handle;
    closes++;
    return STATUS_SUCCESS;
}

#include "extracted_registry.inc"

static void report( const char *name, int passed )
{
    printf( "%s %s\n", passed ? "PASS" : "FAIL", name );
    failures += !passed;
}

static void reset_mocks(void)
{
    calls = closes = fail_call = 0;
    existing = 3;
}

static void test_missing_parents(void)
{
    HANDLE key = NULL;
    NTSTATUS status;

    reset_mocks();
    status = runtime_create_registry_path( paths[ARRAY_SIZE(paths) - 1], &key );
    report( "missing-parents-status", status == STATUS_SUCCESS );
    report( "missing-parents-created", existing == ARRAY_SIZE(paths) && calls == ARRAY_SIZE(paths) );
    report( "missing-parents-handles", key == (HANDLE)(uintptr_t)ARRAY_SIZE(paths) &&
            closes == ARRAY_SIZE(paths) - 1 );
}

static void test_failure(void)
{
    HANDLE key = (HANDLE)(uintptr_t)0xdead;
    NTSTATUS status;

    reset_mocks();
    fail_call = 5;
    status = runtime_create_registry_path( paths[ARRAY_SIZE(paths) - 1], &key );
    report( "failure-status", status == STATUS_ACCESS_DENIED );
    report( "failure-handle", !key && calls == fail_call && closes == fail_call - 1 );
}

static void test_invalid_paths(void)
{
    char path[257];
    HANDLE key = (HANDLE)(uintptr_t)0xdead;

    reset_mocks();
    memset( path, 'a', sizeof(path) - 1 );
    path[0] = '\\';
    path[sizeof(path) - 1] = 0;
    report( "long-path", runtime_create_registry_path( path, &key ) == STATUS_NAME_TOO_LONG && !key && !calls );
    key = (HANDLE)(uintptr_t)0xdead;
    report( "relative-path", runtime_create_registry_path( "Registry", &key ) ==
            STATUS_OBJECT_PATH_SYNTAX_BAD && !key && !calls );
}

int main(void)
{
    test_missing_parents();
    test_failure();
    test_invalid_paths();
    printf( "%u registry bootstrap test failure(s)\n", failures );
    return failures != 0;
}
