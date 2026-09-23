#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/virtual.c').read_text()
start = source.index('NTSTATUS WINAPI NtWriteVirtualMemory(')
end = source.index('\n}\n', start) + 3
function = source[start:end]

fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t NTSTATUS;
typedef void *HANDLE;
typedef size_t SIZE_T;
#define WINAPI
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_PARTIAL_COPY ((NTSTATUS)0x8000000d)
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xc0000005)

struct mock_request { HANDLE handle; uintptr_t addr; };
struct mock_reply { SIZE_T written; };
static struct mock_request server_request;
static struct mock_reply server_reply;
static NTSTATUS local_status, server_status;
static unsigned int local_calls, server_calls;
static int source_readable = 1;

static HANDLE GetCurrentProcess(void) { return (HANDLE)(intptr_t)-1; }
static int virtual_check_buffer_for_read(const void *buffer, SIZE_T size)
{
    (void)buffer;
    (void)size;
    return source_readable;
}
static NTSTATUS virtual_uninterrupted_write_memory(void *addr, const void *buffer, SIZE_T size)
{
    local_calls++;
    if (!local_status) memcpy(addr, buffer, size);
    return local_status;
}
static HANDLE wine_server_obj_handle(HANDLE process) { return process; }
static uintptr_t wine_server_client_ptr(void *ptr) { return (uintptr_t)ptr; }
static void wine_server_add_data(struct mock_request *req, const void *buffer, SIZE_T size)
{
    (void)req;
    (void)buffer;
    (void)size;
}
static NTSTATUS wine_server_call(struct mock_request *req)
{
    (void)req;
    server_calls++;
    return server_status;
}
#define SERVER_START_REQ(name) do { struct mock_request *req = &server_request; \
                                    struct mock_reply *reply = &server_reply;
#define SERVER_END_REQ } while (0)
'''

tests = r'''
int main(void)
{
    unsigned char source[4] = {1, 2, 3, 4}, target[4] = {0};
    SIZE_T written = 99;
    NTSTATUS status;

    status = NtWriteVirtualMemory(GetCurrentProcess(), target, source, sizeof(source), &written);
    assert(status == STATUS_SUCCESS && written == sizeof(source));
    assert(!memcmp(target, source, sizeof(source)) && local_calls == 1 && !server_calls);

    local_status = STATUS_ACCESS_VIOLATION;
    memset(target, 0, sizeof(target));
    written = 99;
    status = NtWriteVirtualMemory(GetCurrentProcess(), target, source, sizeof(source), &written);
    assert(status == STATUS_ACCESS_VIOLATION && !written && local_calls == 2 && !server_calls);
    assert(!memcmp(target, (unsigned char[4]){0}, sizeof(target)));

    local_status = STATUS_SUCCESS;
    server_reply.written = 2;
    written = 99;
    status = NtWriteVirtualMemory((HANDLE)(uintptr_t)4, target, source, sizeof(source), &written);
    assert(status == STATUS_SUCCESS && written == 2 && local_calls == 2 && server_calls == 1);

    source_readable = 0;
    written = 99;
    status = NtWriteVirtualMemory(GetCurrentProcess(), target, source, sizeof(source), &written);
    assert(status == STATUS_PARTIAL_COPY && !written && local_calls == 2 && server_calls == 1);

    puts("NtWriteVirtualMemory current-process and server paths passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='wine-nx-write-memory-') as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(fixture + function + tests)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
