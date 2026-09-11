#!/usr/bin/env python3
"""Check that the Switch's WoW64 unixlib stub tables cover every call of the Wine DLLs they serve.

A unix call indexes the table without a bounds check, so a table shorter than
the DLL's enum would jump to an arbitrary address."""
from pathlib import Path
import re

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
print('PASS: WoW64 unixlib stub tables match ws2_32 (%d) and opengl32 (%d) call counts'
      % (table_size(checks[0][0]), table_size(checks[1][0])))
