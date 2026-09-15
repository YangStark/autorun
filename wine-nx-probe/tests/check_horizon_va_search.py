#!/usr/bin/env python3
"""Exercise Wine's actual mmap probe and map_view retry with native mappings.

Wine's free-range tree can contain libnx stacks/JIT mappings it does not know
about. A 64-bit Wine build must still exhaustively search its bounded guest
address space when exponential mmap probes skip an otherwise usable gap.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/virtual.c').read_text()


def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define __SWITCH__ 1
typedef int BOOL;
typedef uintptr_t UINT_PTR;
typedef uintptr_t ULONG_PTR;
#define TRUE 1
#define FALSE 0
#define MAP_FAILED ((void *)-1)
#define STATUS_NO_MEMORY 1
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define min(a,b) ((size_t)(a)<(size_t)(b)?(a):(b))
static const int is_win64 = 1;
static const uintptr_t granularity_mask = 0xffff;
static const uintptr_t limit_4g = 0x100000000ull;
static const ptrdiff_t max_try_map_step = 0x40000000;
static BOOL increase_try_map_step = TRUE;
static uintptr_t gap_start, gap_end;
static int probes, retries, fail_errno;
static void *anon_mmap_tryfixed(void *ptr, size_t size, int prot, int flags)
{
    uintptr_t addr = (uintptr_t)ptr;
    (void)prot; (void)flags;
    probes++;
    if (!fail_errno && addr >= gap_start && addr <= gap_end && size <= gap_end - addr)
        return ptr;
    errno = fail_errno ? fail_errno : EEXIST;
    return MAP_FAILED;
}
static void clear_native_views(void) { retries++; }
'''
fixture += re.search(r'^struct alloc_area\n\{.*?^\};', source, re.M | re.S)[0] + '\n'
fixture += block(source, 'static void* try_map_free_area(') + '\n'
fixture += r'''
/* Simulate one Wine-free range containing mappings owned by libnx. The real
 * probe below discovers the occupied addresses through mmap's EEXIST result. */
static void *alloc_free_area(void *start, void *end, size_t size, BOOL top_down,
                             int unix_prot, UINT_PTR align_mask)
{
    struct alloc_area area = { .size = size, .step = align_mask + 1,
        .top_down = top_down, .unix_prot = unix_prot, .align_mask = align_mask };
    uintptr_t first = ((uintptr_t)start + align_mask) & ~align_mask;
    if (top_down)
    {
        area.step = -area.step;
        first = ((uintptr_t)end - size) & ~align_mask;
    }
    return try_map_free_area(&area, start, end, (void *)first);
}
static int reserve(void *start, void *end, size_t host_size, int top_down, void **out)
{
    int unix_prot = 0;
    uintptr_t align_mask = granularity_mask;
    void *ptr;
'''
map_view = block(source, 'static NTSTATUS map_view(')
fixture += block(map_view, 'if (!(ptr = alloc_free_area( start, end, host_size,')
fixture += r'''
    *out = ptr;
    return 0;
}
int main(void)
{
    const size_t request = 60032u * 1024; /* race-load reservation from the log */
    void *out = NULL;
    assert(is_win64);
    /* Exponential probes land around 32/64/128 MiB, missing the only suitable
     * aligned start at 80 MiB. An exhaustive retry must find this 64 MiB gap. */
    gap_start = 0x05000000;
    gap_end = gap_start + 0x04000000;
    assert(!reserve((void *)0x10000, (void *)0x40000000, request, FALSE, &out));
    assert((uintptr_t)out == gap_start && retries == 1);
    assert(increase_try_map_step);
    retries = probes = 0;
    assert(!reserve((void *)0x10000, (void *)0x40000000, request, TRUE, &out));
    assert((uintptr_t)out >= gap_start && (uintptr_t)out + request <= gap_end);
    assert(retries == 1 && increase_try_map_step);
    /* The retry must obey the caller's bounds and must not fabricate memory
     * when the remaining gap is too small or mmap fails for another reason. */
    retries = probes = 0;
    assert(reserve((void *)0x10000, (void *)gap_start, request, FALSE, &out) == STATUS_NO_MEMORY);
    assert(increase_try_map_step);
    gap_end = gap_start + request - 0x1000;
    assert(reserve((void *)0x10000, (void *)0x10000000, request, FALSE, &out) == STATUS_NO_MEMORY);
    fail_errno = ENOMEM;
    probes = 0;
    assert(reserve((void *)0x10000, (void *)0x10000000, request, FALSE, &out) == STATUS_NO_MEMORY);
    assert(probes == 2 && increase_try_map_step);
    /* Fast path: a directly available range needs no retry. */
    fail_errno = 0;
    gap_end = gap_start + request;
    retries = probes = 0;
    assert(!reserve((void *)gap_start, (void *)gap_end, request, FALSE, &out));
    assert(probes == 1 && !retries);
    puts("Horizon VA search: 64-bit host finds skipped guest gaps, respects bounds and preserves failures");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'va_search.c'
    exe = Path(tmp) / 'va_search'
    c.write_text(fixture)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
