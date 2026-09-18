#!/usr/bin/env python3
"""Run ntdll's directory listing (read_directory_data_readdir in
dlls/ntdll/unix/file.c) as built for Horizon, over a directory copied from a Mac.

macOS has nowhere on a FAT card to put a file's resource fork, so it writes the
fork of FILE to a second file named ._FILE. Halo asks for every DLL in its
Controls directory, so it loads ._CONTROLS.DLL, gets STATUS_INVALID_IMAGE_FORMAT
for four kilobytes of Apple metadata, and tells the player that one of its own
files is missing or corrupted."""
from pathlib import Path
import subprocess
import tempfile
import os

root = Path(__file__).resolve().parents[2]
file_c = (root / 'dlls/ntdll/unix/file.c').read_text()

def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# The listing runs with the directory as the working directory, which is what
# lets it look for the file a fork belongs to by name.
listing = function(file_c, 'NTSTATUS WINAPI NtQueryDirectoryFile')
assert listing.index('fchdir( fd )') < listing.index('get_cached_dir_data')

fixture = r'''
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int BOOL;
typedef int NTSTATUS;
typedef unsigned short WCHAR;
#define FALSE 0
#define TRUE  1
#define STATUS_SUCCESS       0
#define STATUS_NO_MEMORY     ((NTSTATUS)0xc0000017)
#define STATUS_NO_SUCH_FILE  ((NTSTATUS)0xc000000f)
#define MAX_DIR_ENTRY_LEN 255

typedef struct { unsigned short Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING;

static char listed[64][256];
static unsigned int listed_count;
static char reported_line[512];

struct dir_data { int unused; };

static BOOL append_entry( struct dir_data *data, const char *long_name,
                          const char *short_name, const UNICODE_STRING *mask )
{
    snprintf( listed[listed_count++], 256, "%s", long_name );
    return TRUE;
}

void wine_nx_runtime_trace( const char *msg )
{
    snprintf( reported_line, sizeof(reported_line), "%s", msg );
}

@BODY@

static int was_listed( const char *name )
{
    unsigned int i;

    for (i = 0; i < listed_count; i++) if (!strcmp( listed[i], name )) return 1;
    return 0;
}

int main( int argc, char **argv )
{
    struct dir_data data = { 0 };

    assert( !chdir( argv[1] ) );
    assert( read_directory_data_readdir( &data, NULL ) == STATUS_SUCCESS );

    /* The files the card really holds are all listed. */
    assert( was_listed( "." ) && was_listed( ".." ) );
    assert( was_listed( "CONTROLS.DLL" ) );
    assert( was_listed( "keyboard.txt" ) );

    /* A fork of a file that is there is that file's, and belongs to no listing,
     * as an NTFS alternate data stream belongs to none. */
    assert( !was_listed( "._CONTROLS.DLL" ) );
    assert( !was_listed( "._keyboard.txt" ) );

    /* A fork whose file is gone is a file of its own; hiding it would leave
     * something on the card that the owner cannot see or delete. */
    assert( was_listed( "._orphan.dll" ) );

    /* A name of its own that merely starts with a dot stays. */
    assert( was_listed( ".hidden" ) );
    assert( was_listed( "._" ) );

    assert( strstr( reported_line, "[FS] 2 macOS resource forks" ) );
    printf( "%u entries listed; %s\n", listed_count, reported_line );
    return 0;
}
'''

body = '\n\n'.join([
    function(file_c, 'static BOOL is_resource_fork'),
    function(file_c, 'static void report_hidden_resource_forks'),
    function(file_c, 'static NTSTATUS read_directory_data_readdir'),
])
# Only the Horizon build hides them: everywhere else a card is not a Mac's.
assert body.count('#ifdef __SWITCH__') == 3 and body.count('#endif') == 3

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    card = tmp / 'Controls'
    card.mkdir()
    for name in ('CONTROLS.DLL', '._CONTROLS.DLL', 'keyboard.txt', '._keyboard.txt',
                 '._orphan.dll', '.hidden', '._'):
        (card / name).write_bytes(b'x')
    source = tmp / 'listing.c'
    source.write_text(fixture.replace('@BODY@', body))
    binary = tmp / 'listing'
    subprocess.run(['cc', '-D__SWITCH__', '-o', str(binary), str(source)], check=True)
    out = subprocess.run([str(binary), str(card)], check=True, capture_output=True, text=True)
    print(out.stdout.strip())
