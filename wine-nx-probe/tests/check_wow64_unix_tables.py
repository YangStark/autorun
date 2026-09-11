#!/usr/bin/env python3
"""Check that the Switch's WoW64 unixlib stub tables cover every call of the Wine DLLs they serve.

A unix call indexes the table without a bounds check, so a table shorter than
the DLL's enum would jump to an arbitrary address."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
stubs = (root / 'wine-nx-probe/source/ws2_32_unix_stub.c').read_text()

def enum_count(header, enum, last):
    text = (root / header).read_text()
    body = re.search(r'enum %s\n\{(.*?)\};' % enum, text, re.S).group(1)
    names = [n for n in re.findall(r'^\s*(\w+)\s*,?\s*$', body, re.M)]
    assert names[-1] == last, (header, names[-1])
    return len(names) - 1

def table_size(name):
    return int(re.search(r'const unixlib_entry_t %s\[(\d+)\]' % name, stubs).group(1))

checks = [
    ('wine_nx_ws2_32_wow64_unix_funcs', 'dlls/ws2_32/ws2_32_private.h', 'ws_unix_funcs', 'ws_unix_funcs_count'),
    ('wine_nx_opengl32_wow64_unix_funcs', 'dlls/opengl32/unixlib.h', 'unix_funcs', 'funcs_count'),
]
for table, header, enum, last in checks:
    count = enum_count(header, enum, last)
    size = table_size(table)
    assert size == count, f'{table} has {size} entries, {header} defines {count}'
# opengl32's DllMain needs its first three calls to succeed.
opengl = stubs[stubs.index('wine_nx_opengl32_wow64_unix_funcs[3102]'):]
assert re.match(r'[^{]*\{\s*stub_success,[^\n]*\n\s*stub_success,[^\n]*\n\s*stub_success,', opengl)
for table, *_ in checks:
    count = table.replace('_funcs', '_count')
    assert f'const unsigned int {count} = ARRAY_SIZE({table});' in stubs, f'{count} must be the size of {table}'

# The x86 unix call gate (loader.c) and the static table dispatch (virtual.c),
# compiled against small tables: build 18 refused every handle but ntdll's,
# so opengl32's process_attach failed with STATUS_INVALID_HANDLE.
def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

virtual = (root / 'dlls/ntdll/unix/virtual.c').read_text()
loader = (root / 'dlls/ntdll/unix/loader.c').read_text()
libs = virtual[virtual.index('static const struct\n{\n    const char             *name;'):]
libs = libs[:libs.index('};') + 2]
fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int NTSTATUS;
typedef unsigned int ULONG;
typedef unsigned long UINT_PTR;
typedef unsigned long long unixlib_handle_t;
typedef NTSTATUS (*unixlib_entry_t)( void * );
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xc0000008)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ULongToPtr(ul) ((void *)(UINT_PTR)(ul))
#define CHECK(x) do { if (!(x)) { printf( "FAIL line %d: %s\n", __LINE__, #x ); exit( 1 ); } } while (0)
static int last;
static NTSTATUS ntdll_call( void *args ) { last = 1; return (NTSTATUS)(UINT_PTR)args; }
static NTSTATUS ws2_call( void *args ) { last = 2; return (NTSTATUS)(UINT_PTR)args; }
static NTSTATUS gl_call( void *args ) { last = 3; return 0; }
static const unixlib_entry_t unix_call_wow64_funcs[] = { ntdll_call, ntdll_call };
const unixlib_entry_t wine_nx_ws2_32_wow64_unix_funcs[5] = { ws2_call, ws2_call, ws2_call, ws2_call, ws2_call };
const unixlib_entry_t wine_nx_opengl32_wow64_unix_funcs[3] = { gl_call, gl_call, gl_call };
const unsigned int wine_nx_ws2_32_wow64_unix_count = 5;
const unsigned int wine_nx_opengl32_wow64_unix_count = 3;
''' + libs + '\n' + function(virtual, 'NTSTATUS wine_nx_call_static_wow64_unix') + '\n' + \
    function(loader, 'NTSTATUS wine_nx_call_ntdll_wow64') + r'''
#define H(t) ((unixlib_handle_t)(UINT_PTR)(t))
int main(void)
{
    CHECK( wine_nx_call_ntdll_wow64( H(unix_call_wow64_funcs), 1, 7 ) == 7 && last == 1 );
    CHECK( wine_nx_call_ntdll_wow64( H(unix_call_wow64_funcs), 2, 7 ) == STATUS_INVALID_PARAMETER );
    last = 0;
    CHECK( wine_nx_call_ntdll_wow64( H(wine_nx_opengl32_wow64_unix_funcs), 0, 0x1000 ) == 0 && last == 3 );
    CHECK( wine_nx_call_ntdll_wow64( H(wine_nx_ws2_32_wow64_unix_funcs), 4, 9 ) == 9 && last == 2 );
    last = 0;
    CHECK( wine_nx_call_ntdll_wow64( H(wine_nx_opengl32_wow64_unix_funcs), 3, 0 ) == STATUS_INVALID_PARAMETER );
    CHECK( wine_nx_call_ntdll_wow64( H(wine_nx_ws2_32_wow64_unix_funcs), 5, 0 ) == STATUS_INVALID_PARAMETER );
    CHECK( wine_nx_call_ntdll_wow64( H(wine_nx_ws2_32_wow64_unix_funcs + 1), 0, 0 ) == STATUS_INVALID_HANDLE );
    CHECK( wine_nx_call_ntdll_wow64( 0, 0, 0 ) == STATUS_INVALID_HANDLE );
    CHECK( wine_nx_call_ntdll_wow64( 0xdeadbeef, 0, 0 ) == STATUS_INVALID_HANDLE && !last );
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source, binary = Path(tmp) / 'gate.c', Path(tmp) / 'gate'
    source.write_text(fixture)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: WoW64 unixlib stub tables match ws2_32 (%d) and opengl32 (%d) call counts; the x86 unix call gate '
      'reaches ntdll and both static tables within their sizes and refuses other handles'
      % (table_size(checks[0][0]), table_size(checks[1][0])))
