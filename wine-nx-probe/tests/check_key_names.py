#!/usr/bin/env python3
"""The controls and the keys they can send (wine-nx-probe/source/key_names.h),
which the launcher's Controls screen writes into keys.txt and the runtime reads
back. Every control the runtime knows has to be on the screen, and every key the
screen offers has to be one Windows names."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = (root / 'wine-nx-probe/source/key_names.h').read_text()
runtime = (root / 'wine-nx-probe/source/runtime.c').read_text()

# The runtime's own list of control names, which keys.txt is written against.
names = runtime[runtime.index('wine_nx_pad_key_names[WINE_NX_KEY_COUNT] ='):]
names = re.findall(r'"([A-Z]+)"', names[:names.index('};')])
shown = re.findall(r'\{\s*"([A-Z]+)",\s*"', header)
assert sorted(names) == sorted(shown), (sorted(set(names) ^ set(shown)))

# And what each sends with no line of its own is what the runtime starts with.
defaults = runtime[runtime.index('unsigned short wine_nx_pad_keys[WINE_NX_KEY_COUNT] ='):]
defaults = defaults[:defaults.index('};')]
codes = [int(c, 0) for c in re.findall(r'(0x[0-9a-f]+|\b0\b)', defaults.split('=', 1)[1])]
assert len(codes) == len(names), (len(codes), len(names))
runtime_default = dict(zip(names, codes))
for name, sends in re.findall(r'\{\s*"([A-Z]+)",[^}]*?(0x[0-9a-f]+)\s*\}', header):
    assert runtime_default[name] == int(sends, 0), (name, sends, hex(runtime_default[name]))

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>

@HEADER@

int main( void )
{
    char text[64];
    int i, j;

    /* No code named twice, and no name given to two codes. */
    for (i = 0; i < WINE_NX_KEY_NAME_COUNT; i++)
        for (j = i + 1; j < WINE_NX_KEY_NAME_COUNT; j++)
        {
            assert( wine_nx_key_names[i].code != wine_nx_key_names[j].code );
            assert( strcmp( wine_nx_key_names[i].name, wine_nx_key_names[j].name ) );
        }
    /* And every one is a virtual-key code, which is a byte. */
    for (i = 0; i < WINE_NX_KEY_NAME_COUNT; i++) assert( wine_nx_key_names[i].code <= 0xff );

    assert( wine_nx_key_index( 0x57 ) >= 0 );
    assert( !strcmp( wine_nx_key_names[wine_nx_key_index( 0x57 )].name, "W" ) );
    assert( wine_nx_key_index( 0x00 ) == 0 );
    assert( wine_nx_key_index( 0xf5 ) == -1 );

    /* What a control with no key of its own says depends on the control. */
    assert( !strcmp( wine_nx_key_label( 0, 0, text, sizeof(text) ), "Left mouse button" ) );
    assert( !strcmp( wine_nx_key_label( 1, 0, text, sizeof(text) ), "Right mouse button" ) );
    assert( !strcmp( wine_nx_key_label( 2, 0, text, sizeof(text) ), "Nothing" ) );
    assert( !strcmp( wine_nx_key_label( 16, 0, text, sizeof(text) ), "What the d-pad sends" ) );
    assert( !strcmp( wine_nx_key_label( 2, 0x0d, text, sizeof(text) ), "Enter" ) );
    /* A code from a hand-written file that the list does not name. */
    assert( !strcmp( wine_nx_key_label( 2, 0xf5, text, sizeof(text) ), "0xf5" ) );

    printf( "%d controls, %d keys named, none twice\n", WINE_NX_CONTROL_COUNT, WINE_NX_KEY_NAME_COUNT );
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'keys.c'
    source.write_text(fixture.replace('@HEADER@', header))
    binary = Path(tmp) / 'keys'
    subprocess.run(['cc', '-o', str(binary), str(source)], check=True)
    print(subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.strip())
