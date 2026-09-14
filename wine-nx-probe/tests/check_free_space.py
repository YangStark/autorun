#!/usr/bin/env python3
"""Run ntdll's free space query (get_full_size_info in dlls/ntdll/unix/file.c)
as built for Horizon, on a card that answers like libnx's statvfs (fsdev_statvfs:
blocks of one byte). WarCraft III checks the space before saving a profile, with
GetDiskFreeSpaceExA or, without it, GetDiskFreeSpaceA, on the save directory,
which Horizon opens with no descriptor. Before build 93 that query went to the
Horizon server, which has no get_volume_info, so the game saw no free space."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
file_c = (root / 'dlls/ntdll/unix/file.c').read_text()
horizon = (root / 'dlls/ntdll/unix/horizon.c').read_text()

def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# The Horizon volume query examines directories by name, not through the server.
query = function(file_c, 'NTSTATUS WINAPI NtQueryVolumeInformationFile')
switch_path = query.index('fd_type = FD_TYPE_DIR;')
assert switch_path < query.index('SERVER_START_REQ( get_volume_info )')
assert 'else if (fd == -1)' in query
assert 'get_volume_info' not in horizon

fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef void *HANDLE;
typedef int NTSTATUS;
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef union { struct { unsigned int LowPart; int HighPart; } u; long long QuadPart; } LARGE_INTEGER;
typedef struct
{
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER CallerAvailableAllocationUnits;
    LARGE_INTEGER ActualAvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION;
#define STATUS_SUCCESS                0
#define STATUS_INVALID_DEVICE_REQUEST ((NTSTATUS)0xc0000010)
#define STATUS_OBJECT_TYPE_MISMATCH   ((NTSTATUS)0xc0000024)
#define STATUS_OBJECT_NAME_NOT_FOUND  ((NTSTATUS)0xc0000034)

/* newlib's, with libnx filling it (64-bit block counts on devkitA64). */
struct statvfs { unsigned long f_bsize, f_frsize; unsigned long long f_blocks, f_bfree, f_bavail; };

/* The card and the handle's file. */
static struct { const char *name; mode_t mode; int exists; unsigned long long total, free; NTSTATUS name_status; } card;
static char stat_name[256], statvfs_name[256];
static int names_out;

static unsigned int server_get_unix_name( HANDLE handle, char **unix_name )
{
    if (card.name_status) return card.name_status;
    *unix_name = strdup( card.name );
    names_out++;
    return 0;
}

static NTSTATUS errno_to_status( int err )
{
    return err == ENOENT ? STATUS_OBJECT_NAME_NOT_FOUND : (NTSTATUS)0xc0000001;
}

static int fake_stat( const char *path, struct stat *st )
{
    snprintf( stat_name, sizeof(stat_name), "%s", path );
    if (!card.exists)
    {
        errno = ENOENT;
        return -1;
    }
    memset( st, 0, sizeof(*st) );
    st->st_mode = card.mode;
    return 0;
}

/* fsdev_statvfs: sizes in bytes. */
static int fake_statvfs( const char *path, struct statvfs *buf )
{
    snprintf( statvfs_name, sizeof(statvfs_name), "%s", path );
    buf->f_bsize = buf->f_frsize = 1;
    buf->f_blocks = card.total;
    buf->f_bfree = buf->f_bavail = card.free;
    return 0;
}

#define stat(path, st) fake_stat( path, st )
#define statvfs(path, buf) fake_statvfs( path, buf )
#define free(ptr) (names_out--, free( ptr ))
'''

tests = r'''
#undef free

/* kernelbase GetDiskFreeSpaceExW: available units times the unit size. */
static unsigned long long free_space_ex( const FILE_FS_FULL_SIZE_INFORMATION *info )
{
    ULONG units = info->SectorsPerAllocationUnit * info->BytesPerSector;
    return info->CallerAvailableAllocationUnits.QuadPart * units;
}

/* GetDiskFreeSpaceA on an NT version (32-bit cluster counts), as game.dll uses
 * it without GetDiskFreeSpaceExA: sectors times bytes widened by mul, then
 * times the free clusters with _allmul (0x6f6d7dad). */
static unsigned long long free_space_war3_fallback( const FILE_FS_FULL_SIZE_INFORMATION *info )
{
    unsigned int sectors = info->SectorsPerAllocationUnit, bytes = info->BytesPerSector;
    unsigned int clusters = info->CallerAvailableAllocationUnits.u.LowPart;
    return (unsigned long long)sectors * bytes * clusters;
}

static void check_card( const char *what, unsigned long long total, unsigned long long free_bytes )
{
    FILE_FS_FULL_SIZE_INFORMATION info;

    card.name = "sdmc:/switch/wine/drive_c/WarCraft III/save";
    card.mode = S_IFDIR | 0777;
    card.exists = 1;
    card.total = total;
    card.free = free_bytes;
    card.name_status = 0;
    stat_name[0] = statvfs_name[0] = 0;

    assert( get_full_size_info( (HANDLE)0x40, -1, &info ) == STATUS_SUCCESS );
    assert( !names_out );
    assert( !strcmp( stat_name, card.name ) && !strcmp( statvfs_name, card.name ) );
    assert( info.BytesPerSector * info.SectorsPerAllocationUnit == 4096 );
    assert( (unsigned long long)info.TotalAllocationUnits.QuadPart == total / 4096 );
    assert( (unsigned long long)info.CallerAvailableAllocationUnits.QuadPart == free_bytes / 4096 );
    assert( free_space_ex( &info ) == free_bytes / 4096 * 4096 );
    assert( free_space_war3_fallback( &info ) == free_space_ex( &info ) );
    printf( "%s: %llu bytes free reported as %llu\n", what, free_bytes, free_space_ex( &info ) );
}

int main( void )
{
    FILE_FS_FULL_SIZE_INFORMATION info;

    check_card( "512 GB card", 512000000000ull, 123456789012ull );
    check_card( "2 TB card", 2000000000000ull, 1900000000000ull );
    check_card( "full card", 64000000000ull, 1000ull );

    /* A file handle is examined the same way. */
    card.mode = S_IFREG | 0666;
    assert( get_full_size_info( (HANDLE)0x44, 7, &info ) == STATUS_SUCCESS && !names_out );

    /* A named pipe or the like is no volume. */
    card.mode = S_IFIFO | 0666;
    assert( get_full_size_info( (HANDLE)0x44, 7, &info ) == STATUS_INVALID_DEVICE_REQUEST && !names_out );

    /* A directory gone meanwhile reports why, and frees its name. */
    card.mode = S_IFDIR | 0777;
    card.exists = 0;
    assert( get_full_size_info( (HANDLE)0x40, -1, &info ) == STATUS_OBJECT_NAME_NOT_FOUND && !names_out );

    /* A handle without a unix name reports the server's status. */
    card.exists = 1;
    card.name_status = STATUS_OBJECT_TYPE_MISMATCH;
    assert( get_full_size_info( (HANDLE)0x48, -1, &info ) == STATUS_OBJECT_TYPE_MISMATCH && !names_out );

    printf( "PASS: free space by unix name\n" );
    return 0;
}
'''

source = (fixture + function(file_c, 'static NTSTATUS get_full_size_info(HANDLE') + '\n' + tests)
with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / 'free_space.c'
    exe = Path(tmp) / 'free_space'
    c_file.write_text(source)
    subprocess.run(['cc', '-std=gnu99', '-D__SWITCH__', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Werror',
                    '-o', str(exe), str(c_file)], check=True)
    subprocess.run([str(exe)], check=True)
