#!/usr/bin/env python3
"""Exercise the actual Horizon close handler with host files and a handle fixture."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/horizon.c').read_text()
a = source.index('static unsigned int horizon_server_close_object_handle(')
b = source.index('static unsigned int horizon_server_duplicate_object_handle', a)
fixture = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define HORIZON_STATUS_SUCCESS 0
#define HORIZON_STATUS_INVALID_HANDLE 0xc0000008u
#define HORIZON_SERVER_OBJECT_FILE 1
#define horizon_trace(...) ((void)0)
struct horizon_server_object { unsigned refs, type, file_options; char *file_name; int file_is_dir, file_fd; };
struct horizon_server_handle_entry { unsigned handle; struct horizon_server_object *object; struct horizon_server_handle_entry *next; };
static struct horizon_server_handle_entry *horizon_server_handles;
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned horizon_server_errno_status(int e) { return 0xc0000000u | e; }
static void horizon_server_free_object(struct horizon_server_object *o) { if (o->file_fd != -1) close(o->file_fd); free(o->file_name); free(o); }
static int expected_closed_fd = -1;
static int checked_unlink(const char *path) {
    if (expected_closed_fd != -1) assert(fcntl(expected_closed_fd, F_GETFD) == -1 && errno == EBADF);
    return unlink(path);
}
#define unlink checked_unlink
static void setup(const char *name, unsigned options, int directory, int duplicates)
{
    struct horizon_server_object *o = calloc(1, sizeof(*o));
    o->file_fd = open(name, O_RDONLY); expected_closed_fd = o->file_fd; o->refs = duplicates; o->type = HORIZON_SERVER_OBJECT_FILE;
    o->file_options = options; o->file_name = strdup(name); o->file_is_dir = directory;
    for (int i = 1; i <= duplicates; i++) {
        struct horizon_server_handle_entry *h = calloc(1, sizeof(*h));
        h->handle = i; h->object = o; h->next = horizon_server_handles; horizon_server_handles = h;
    }
}
'''
tests = r'''
int main(void)
{
    char path[] = "/tmp/wine-nx-delete-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    setup(path, 0, 0, 1);
    assert(!horizon_server_close_object_handle(1) && access(path, F_OK) == 0);
    setup(path, 0x1000, 0, 2);
    assert(!horizon_server_close_object_handle(1) && access(path, F_OK) == 0);
    assert(!horizon_server_close_object_handle(2) && access(path, F_OK) == -1 && errno == ENOENT);
    assert(horizon_server_close_object_handle(2) == HORIZON_STATUS_INVALID_HANDLE);
    /* Trying file unlink on a directory must not silently succeed. */
    assert(mkdir(path, 0700) == 0);
    setup(path, 0x1000, 0, 1);
    assert(horizon_server_close_object_handle(1) != 0 && access(path, F_OK) == 0);
    setup(path, 0x1000, 1, 1);
    assert(!horizon_server_close_object_handle(1) && access(path, F_OK) == -1);
    puts("Horizon close: normal close, final duplicate deletion, error propagation and directory deletion passed");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-delete-test-') as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text('#include <sys/stat.h>\n' + fixture + source[a:b] + tests)
    subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
