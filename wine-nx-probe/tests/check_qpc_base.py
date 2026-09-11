#!/usr/bin/env python3
"""Check the Switch performance counter against what MSVC programs do with it.

MSVC's std::chrono::steady_clock::now() returns QueryPerformanceCounter * 100
nanoseconds when the frequency is 10 MHz, as Wine's is. OpenTTD's video loop
compares that against time points that start at 0 and sleeps until the next
one. Build 29 counted from 1601 (gettimeofday minus a server start time the
Horizon server never reports): the product overflowed to a negative time, so
OpenTTD never drew and slept for the maximum time.

The test compiles sync.c's monotonic_counter() with __SWITCH__ defined, at an
uptime of 3 days with the wall clock in 2026. --baseline removes the Switch
branch and must reproduce the failure."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sync = (root / 'dlls/ntdll/unix/sync.c').read_text()
start = sync.index('#ifdef __SWITCH__\nextern unsigned long long horizon_interrupt_time(void);')
end = sync.index('\n}\n', sync.index('static inline ULONGLONG monotonic_counter(void)', start)) + 3
counter = sync[start:end]
if '--baseline' in sys.argv:
    counter = re.sub(r'#elif defined\(__SWITCH__\).*?(?=#elif defined\(HAVE_CLOCK_GETTIME\))', '', counter, flags=re.S)
    assert 'horizon_interrupt_time()' not in counter.split('static inline')[1]

fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <limits.h>
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
#undef __APPLE__  /* compiled on a Mac, tested as the Switch build */
#define __SWITCH__ 1
#define TICKSPERSEC 10000000
#define SECS_1601_TO_1970 ((369 * 365 + 89) * (ULONGLONG)86400)
#define TICKS_1601_TO_1970 (SECS_1601_TO_1970 * TICKSPERSEC)
static LONGLONG server_start_time;  /* the Horizon server leaves it 0 */
static ULONGLONG ticks_from_time_t( long t ) { return t * (ULONGLONG)TICKSPERSEC + TICKS_1601_TO_1970; }
static const ULONGLONG uptime = 3 * 86400ULL * TICKSPERSEC;
unsigned long long horizon_interrupt_time(void) { return uptime; }
#define gettimeofday fake_gettimeofday
static int fake_gettimeofday( struct timeval *tv, void *tz ) { tv->tv_sec = 1789000000; tv->tv_usec = 0; return 0; }
''' + counter + r'''
#define CHECK(x) do { if (!(x)) { printf( "FAIL: %s\n", #x ); exit( 1 ); } } while (0)
int main(void)
{
    ULONGLONG qpc = monotonic_counter();
    /* steady_clock::now() in nanoseconds, as MSVC computes it, without UB. */
    CHECK( qpc <= LLONG_MAX / 100 );
    long long now = (long long)qpc * 100, next_draw_tick = 0;
    CHECK( qpc == uptime );
    CHECK( now >= next_draw_tick );  /* OpenTTD draws its first frame */
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source, binary = Path(tmp) / 'qpc.c', Path(tmp) / 'qpc'
    source.write_text(fixture)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                    '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    print(result.stdout, end='')
    if result.returncode:
        sys.exit(1)
print('PASS: the Switch performance counter counts from boot; MSVC steady_clock nanoseconds fit in 64 bits '
      'and OpenTTD reaches its first draw tick')
