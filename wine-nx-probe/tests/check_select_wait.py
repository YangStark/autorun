#!/usr/bin/env python3
"""Exercise the production wait loop and wait-any/all selection on the host."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[2] / 'dlls/ntdll/unix/horizon.c').read_text()
def extract(start, end):
    return source[source.index(start):source.index(end, source.index(start))]
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#define HORIZON_STATUS_SUCCESS 0
#define HORIZON_STATUS_TIMEOUT 0x102
#define HORIZON_STATUS_INVALID_PARAMETER 0xc000000du
#define HORIZON_STATUS_ABANDONED_WAIT_0 0x80u
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
typedef struct { long long QuadPart; } LARGE_INTEGER;
struct horizon_server_connection { int reply_fd; };
struct horizon_select_request { unsigned size; long long timeout; };
struct horizon_select_reply { struct { unsigned error; } header; unsigned signaled; };
struct horizon_select_wait_op { int op; unsigned handles[4]; };
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned states[4], consumed[4], abandoned[4];
static unsigned horizon_server_wait_object_locked(unsigned h, int consume) {
    if (states[h]) return states[h];
    if (consume) {
        consumed[h]++; states[h] = HORIZON_STATUS_TIMEOUT;
        if (abandoned[h]) { abandoned[h] = 0; return HORIZON_STATUS_ABANDONED_WAIT_0; }
    }
    return 0;
}
static long long ticks;
static unsigned calls, ready_after, signals, reply_status;
static unsigned horizon_server_select_status(const struct horizon_select_request *r,
 const unsigned char *d, unsigned n, int initial) {
    (void)r; (void)d; (void)n; signals += initial;
    return ++calls >= ready_after ? 0 : HORIZON_STATUS_TIMEOUT;
}
static void NtQueryPerformanceCounter(LARGE_INTEGER *now, void *freq) { (void)freq; now->QuadPart = ticks; }
static void NtQuerySystemTime(LARGE_INTEGER *now) { now->QuadPart = ticks; }
static void test_sleep(unsigned us) { ticks += us * 10; }
#define usleep test_sleep
static int horizon_server_write_reply(int fd, const void *data, unsigned size, const void *extra, unsigned n) {
    (void)fd; (void)size; (void)extra; (void)n;
    reply_status = ((const struct horizon_select_reply *)data)->header.error; return 0;
}
'''
tests = r'''
static void run(long long timeout, unsigned ready, unsigned expected, unsigned attempts) {
    struct horizon_select_request r = { 8, timeout };
    struct horizon_server_connection c = { 1 };
    ticks = 100000; calls = signals = 0; ready_after = ready;
    horizon_server_handle_select(&c, (const unsigned char *)&r, NULL, 0);
    assert(reply_status == expected && calls == attempts && signals == 1);
}
int main(void) {
    run(0, 2, 0x102, 1); /* Zero timeout does not block. */
    run(-120000, 100, 0x102, 3); /* Server monotonic deadline. */
    run(120000, 100, 0x102, 3); /* NT absolute wall deadline. */
    run(-200000, 3, 0, 3); /* Signal arrives before deadline. */
    run(0x7fffffffffffffffLL, 5, 0, 5); /* Infinite waits remain pending. */
    struct horizon_select_wait_op op = { 0, { 0, 1 } };
    unsigned size = offsetof(struct horizon_select_wait_op, handles[2]);
    states[0] = 0x102; states[1] = 0;
    assert(horizon_server_select_wait(&op, size, 0) == 1 && consumed[1] == 1);
    states[0] = 0; states[1] = 0x102;
    assert(horizon_server_select_wait(&op, size, 1) == 0x102 && consumed[0] == 0);
    states[1] = 0;
    assert(horizon_server_select_wait(&op, size, 1) == 0 && consumed[0] == 1 && consumed[1] == 2);
    /* An abandoned mutex keeps its index for wait-any and taints wait-all. */
    states[0] = 0x102; states[1] = 0; abandoned[1] = 1;
    assert(horizon_server_select_wait(&op, size, 0) == 0x81 && consumed[1] == 3);
    states[0] = states[1] = 0; abandoned[0] = 1;
    assert(horizon_server_select_wait(&op, size, 1) == 0x80 && consumed[0] == 2 && consumed[1] == 4);
    puts("Horizon waits: deadlines, infinite pending, signal once, wait-any index, atomic wait-all, abandoned passed");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-wait-test-') as tmp:
    c = Path(tmp) / 'test.c'; exe = Path(tmp) / 'test'
    c.write_text(fixture + extract('static unsigned int horizon_server_select_wait(', 'static unsigned int horizon_server_select_signal_and_wait(') + extract('static int horizon_server_handle_select(', 'static void *horizon_server_thread(') + tests)
    subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
