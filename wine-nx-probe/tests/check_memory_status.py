#!/usr/bin/env python3
"""Run horizon_get_memory_info on made-up Horizon and heap figures, and derive
what GlobalMemoryStatusEx would report from them.

libnx claims the whole heap from Horizon at startup, so InfoType_UsedMemorySize
stays a few megabytes below InfoType_TotalMemorySize however little the process
has allocated. Wine's ullAvailPageFile is exactly total - used, and Fallout New
Vegas quits with "Not enough memory to run application." unless it is at least
512 MB: it read 4 MB on build 109. What a program can still get is what malloc
holds unused plus what it has not taken from the heap."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/horizon.c').read_text()

PAGE = 0x1000
MB = 1 << 20
NEEDED = 0x20000000  # what Fallout New Vegas wants of ullAvailPageFile


def block(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fixture = f'''
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long long u64;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define CUR_PROCESS_HANDLE 0xffff8001u
enum {{ InfoType_TotalMemorySize = 6, InfoType_UsedMemorySize = 7 }};

/* The fields newlib's mallinfo returns, which are narrower than a 3 GB heap. */
struct mallinfo {{ int arena, ordblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; }};
static struct mallinfo fake_mallinfo;
static struct mallinfo mallinfo( void ) {{ return fake_mallinfo; }}
char *fake_heap_start, *fake_heap_end;
static u64 fake_total, fake_used;
static int fake_rc;

static int svcGetInfo( u64 *out, int id, unsigned int handle, u64 sub )
{{
    (void)handle; (void)sub;
    if (fake_rc) return fake_rc;
    *out = id == InfoType_TotalMemorySize ? fake_total : fake_used;
    return 0;
}}

{block('void horizon_get_memory_info(')}

int main( int argc, char **argv )
{{
    unsigned long long total, used;

    (void)argc;
    fake_total = strtoull( argv[1], NULL, 0 );
    fake_used = strtoull( argv[2], NULL, 0 );
    fake_heap_start = (char *)0x10000000;
    fake_heap_end = fake_heap_start + strtoull( argv[3], NULL, 0 );
    fake_mallinfo.arena = (int)strtoll( argv[4], NULL, 0 );
    fake_mallinfo.fordblks = (int)strtoll( argv[5], NULL, 0 );
    fake_rc = (int)strtol( argv[6], NULL, 0 );
    horizon_get_memory_info( &total, &used );
    printf( "%llu %llu\\n", total, used );
    return 0;
}}
'''


def run(binary, total, used, heap_size, arena, fordblks, rc=0):
    out = subprocess.run([str(binary), str(total), str(used), str(heap_size), str(arena), str(fordblks), str(rc)],
                         check=True, capture_output=True, text=True).stdout.split()
    total, used = int(out[0]), int(out[1])
    # kernelbase's GlobalMemoryStatusEx: AvailPageFile is TotalCommitLimit minus
    # TotalCommittedPages, which get_performance_info builds from total - used.
    freeram = total - used
    avail_page_file = (((total + PAGE) // PAGE) - ((total + PAGE - freeram) // PAGE)) * PAGE
    return total, used, avail_page_file


with tempfile.TemporaryDirectory(prefix='wine-nx-memory-status-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(fixture)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    test = tmp / 'test'

    # The build 109 Fallout run: Horizon said 3189 MB total, 3185 MB used, while
    # the runtime's own [PROGRESS] line had 249 MB of heap in use, 2883 MB free.
    total, used, avail = run(test, 3189 * MB, 3185 * MB, 3132 * MB, 300 * MB, 51 * MB)
    assert total == 3189 * MB, total
    assert 2800 * MB < total - used < 2900 * MB, (total, used)
    assert avail >= NEEDED, avail
    print(f'hardware figures: {(total - used) // MB} MB free, AvailPageFile {avail // MB} MB '
          f'(the game needs {NEEDED // MB} MB)')

    # Without the heap, only Horizon's few megabytes are free, which is what the
    # game saw and refused.
    _, _, avail_old = run(test, 3189 * MB, 3185 * MB, 0, 0, 0)
    assert avail_old < NEEDED, avail_old

    # A full heap reports what Horizon does, and never more than it.
    total, used, _ = run(test, 3189 * MB, 3185 * MB, 3132 * MB, 3132 * MB, 0)
    assert used == 3185 * MB, used
    # Fields that overflow int stay bounded by the heap and by the total.
    total, used, _ = run(test, 3189 * MB, 3185 * MB, 512 * MB, -(1 << 30), -(1 << 30))
    assert 0 <= used <= total and total - used <= 512 * MB, (total, used)
    # Nothing mapped yet, and a failed svcGetInfo.
    assert run(test, 3189 * MB, 3185 * MB, 0, 0, 0)[1] == 3185 * MB
    assert run(test, 0, 0, 3132 * MB, 0, 0, rc=0xd401) == (0, 0, 0)

print('Memory status: free memory follows the heap, so AvailPageFile passes a 512 MB check')
