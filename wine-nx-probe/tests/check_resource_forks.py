#!/usr/bin/env python3
"""Run the Horizon server's directory listing entry test
(horizon_dir_entry_is_resource_fork in dlls/ntdll/unix/horizon.c) over a game
directory copied from a Mac.

A card is formatted FAT, which has nowhere to keep a file's resource fork, so
macOS writes the fork of FILE to a second file named ._FILE. Halo asks for every
DLL in its Controls directory, so it loads ._CONTROLS.DLL, is told the image is
invalid, and reports that one of its own files is missing or corrupted.

NtQueryDirectoryFile on Horizon does not read the directory itself: it asks the
in-process server for one matching name per call, so this is the only listing a
program sees."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
horizon = (root / 'dlls/ntdll/unix/horizon.c').read_text()
access = (root / 'dlls/ntdll/unix/horizon_file_access.h').read_text()

def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# The listing the server hands out is the one that skips a fork, and it skips it
# before the entry is counted, so a scan that stops and resumes stays in step.
handler = function(horizon, 'static int horizon_server_handle_query_directory_file')
skip = handler.index('horizon_dir_entry_is_resource_fork( object->file_name, de->d_name )')
assert handler.index('if (!strcmp( de->d_name, "." )') < skip < handler.index('raw_index++')
assert 'horizon_report_resource_fork' in handler

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

@BODY@

int main( int argc, char **argv )
{
    const char *dir = argv[1];

    /* The files the card really holds. */
    assert( !horizon_dir_entry_is_resource_fork( dir, "CONTROLS.DLL" ) );
    assert( !horizon_dir_entry_is_resource_fork( dir, "keyboard.txt" ) );

    /* A fork of a file that is there is that file's, and belongs to no listing,
     * as an NTFS alternate data stream belongs to none. */
    assert( horizon_dir_entry_is_resource_fork( dir, "._CONTROLS.DLL" ) );
    assert( horizon_dir_entry_is_resource_fork( dir, "._keyboard.txt" ) );

    /* A fork whose file is gone is a file of its own; hiding it would leave
     * something on the card that the owner cannot see or delete. */
    assert( !horizon_dir_entry_is_resource_fork( dir, "._orphan.dll" ) );

    /* A name of its own that merely starts with a dot stays. */
    assert( !horizon_dir_entry_is_resource_fork( dir, ".hidden" ) );
    assert( !horizon_dir_entry_is_resource_fork( dir, "._" ) );

    /* A device root keeps its slash, so the name below it is found there. */
    assert( !horizon_dir_entry_is_resource_fork( "sdmc:/", "._nothing" ) );

    printf( "the card's own files listed, %s kept out\n", "._CONTROLS.DLL and ._keyboard.txt" );
    return 0;
}
'''

body = '\n\n'.join([
    function(access, 'static inline char *horizon_dir_entry_path'),
    function(horizon, 'static int horizon_dir_entry_is_resource_fork'),
])

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    card = tmp / 'Controls'
    card.mkdir()
    for name in ('CONTROLS.DLL', '._CONTROLS.DLL', 'keyboard.txt', '._keyboard.txt',
                 '._orphan.dll', '.hidden', '._'):
        (card / name).write_bytes(b'x')
    source = tmp / 'forks.c'
    source.write_text(fixture.replace('@BODY@', body))
    binary = tmp / 'forks'
    subprocess.run(['cc', '-o', str(binary), str(source)], check=True)
    out = subprocess.run([str(binary), str(card)], check=True, capture_output=True, text=True)
    print(out.stdout.strip())
