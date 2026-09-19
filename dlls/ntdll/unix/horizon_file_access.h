/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_HORIZON_FILE_ACCESS_H
#define WINE_HORIZON_FILE_ACCESS_H

#include <stdlib.h>
#include <string.h>

/* File generic mapping from server/file.c. Keep this usable by the standalone
 * Horizon build too; tests compare these masks with Wine's winnt.h constants. */
static inline unsigned int horizon_file_map_access( unsigned int access )
{
    unsigned int mapped = access & ~0xf0000000u;
    if (access & 0x80000000u) mapped |= 0x00120089u; /* FILE_GENERIC_READ */
    if (access & 0x40000000u) mapped |= 0x00120116u; /* FILE_GENERIC_WRITE */
    if (access & 0x20000000u) mapped |= 0x001200a0u; /* FILE_GENERIC_EXECUTE */
    if (access & 0x10000000u) mapped |= 0x001f01ffu; /* FILE_ALL_ACCESS */
    return mapped;
}

/* wineserver's check_sharing for a regular file: the handles the path already
 * has open allow what all their sharing modes allow and use what any of them
 * uses (access mapped, sharing as FILE_SHARE_*). A new handle may not use what
 * they do not share, nor refuse to share what they use -- unless it asks for
 * no data access at all, as an attributes query does. The Sims 2 opens each
 * of its packages to read, sharing reads only, then to write while that handle
 * is open: Windows answers that with a sharing violation. */
static inline int horizon_file_sharing_violation( unsigned int existing_access, unsigned int existing_sharing,
                                                  unsigned int access, unsigned int sharing )
{
    const unsigned int read = 0x0021, write = 0x0006, del = 0x00010000; /* READ_DATA|EXECUTE, WRITE|APPEND_DATA */

    if (((access & read) && !(existing_sharing & 1)) ||   /* FILE_SHARE_READ */
        ((access & write) && !(existing_sharing & 2)) ||  /* FILE_SHARE_WRITE */
        ((access & del) && !(existing_sharing & 4)))      /* FILE_SHARE_DELETE */
        return 1;
    if (!(access & (read | write | del))) return 0;
    return ((existing_access & read) && !(sharing & 1)) ||
           ((existing_access & write) && !(sharing & 2)) ||
           ((existing_access & del) && !(sharing & 4));
}

/* FILE_APPEND_DATA is also included in GENERIC_WRITE. Only an append-only
 * handle should force O_APPEND; normal writable handles must support seeking. */
static inline int horizon_file_access_mode( unsigned int access )
{
    unsigned int mapped = horizon_file_map_access( access );
    int read = !!(mapped & 1u), write = !!(mapped & 6u);
    int flags = read && write ? O_RDWR : write ? O_WRONLY : O_RDONLY;
    if ((mapped & 4u) && !(mapped & 2u)) flags |= O_APPEND;
    return flags;
}

/* Status for opening an existing directory without FILE_DIRECTORY_FILE, as
 * wineserver's open_fd decides: CreateFileW with backup semantics may open a
 * directory, FILE_NON_DIRECTORY_FILE refuses one, and a disposition that would
 * create or truncate collides with it. libnx cannot open() a directory, so the
 * Horizon server asks this after open() fails on a path opendir() accepts. */
static inline unsigned int horizon_directory_open_status( unsigned int options, int flags )
{
    if (options & 0x00000040u) return 0xc00000bau;           /* STATUS_FILE_IS_A_DIRECTORY */
    if (flags & (O_EXCL | O_TRUNC)) return 0xc0000035u;      /* STATUS_OBJECT_NAME_COLLISION */
    return 0;
}

/* FileDispositionInformation(Ex), as wineserver's set_fd_disposition decides:
 * FILE_DISPOSITION_ON_CLOSE withdraws a FILE_DELETE_ON_CLOSE given at open, and
 * the file goes at its last close if the disposition or that option asks. */
static inline int horizon_disposition_update( unsigned int *options, unsigned int flags )
{
    if (flags & 0x00000008u) *options &= ~0x00001000u;    /* ON_CLOSE clears FILE_DELETE_ON_CLOSE */
    return (flags & 0x00000001u) || (*options & 0x00001000u); /* FILE_DISPOSITION_DELETE */
}

enum horizon_rename_action
{
    HORIZON_RENAME_MOVE,     /* nothing is in the way */
    HORIZON_RENAME_REPLACE,  /* remove the existing target first */
    HORIZON_RENAME_NOTHING,  /* the target is this file already */
};

/* Renaming onto a name that exists, as wineserver's set_fd_name decides: the
 * same file stays as it is, another collides unless FILE_RENAME_REPLACE_IF_EXISTS,
 * and neither a directory nor an open file can be replaced. */
static inline unsigned int horizon_rename_check( int target_exists, int same_file, int target_is_dir,
                                                 int target_open, unsigned int flags,
                                                 enum horizon_rename_action *action )
{
    *action = HORIZON_RENAME_MOVE;
    if (!target_exists) return 0;
    if (same_file)
    {
        *action = HORIZON_RENAME_NOTHING;
        return 0;
    }
    if (!(flags & 0x00000001u)) return 0xc0000035u;          /* STATUS_OBJECT_NAME_COLLISION */
    if (target_is_dir || target_open) return 0xc0000022u;    /* STATUS_ACCESS_DENIED */
    *action = HORIZON_RENAME_REPLACE;
    return 0;
}

/* End of a directory scan, as Wine's get_cached_dir_data reports it: a handle's
 * first query that matches nothing is STATUS_NO_SUCH_FILE, which FindFirstFileW
 * turns into ERROR_FILE_NOT_FOUND; later queries run out with NO_MORE_FILES. */
static inline unsigned int horizon_dir_scan_end_status( int first_query )
{
    return first_query ? 0xc000000fu /* STATUS_NO_SUCH_FILE */ : 0x80000006u /* STATUS_NO_MORE_FILES */;
}

/* Unix name of a directory entry below dir: "." names dir itself and ".." its
 * parent. A device root keeps its slash ("sdmc:/"). Returns malloc'ed memory. */
static inline char *horizon_dir_entry_path( const char *dir, const char *name )
{
    size_t dir_len = strlen( dir ), name_len = strlen( name );
    char *path;

    while (dir_len > 1 && dir[dir_len - 1] == '/' && dir[dir_len - 2] != ':') dir_len--;
    if (!strcmp( name, ".." ))
    {
        size_t parent = dir_len;

        while (parent && dir[parent - 1] != '/') parent--;
        while (parent > 1 && dir[parent - 1] == '/' && dir[parent - 2] != ':') parent--;
        if (parent) dir_len = parent;
        name_len = 0;
    }
    else if (!strcmp( name, "." )) name_len = 0;

    if (!(path = malloc( dir_len + name_len + 2 ))) return NULL;
    memcpy( path, dir, dir_len );
    if (name_len && dir_len && dir[dir_len - 1] != '/') path[dir_len++] = '/';
    memcpy( path + dir_len, name, name_len );
    path[dir_len + name_len] = 0;
    return path;
}

/* Horizon's FAT file systems ignore ASCII case, and two unix names for one
 * file may also differ in repeated or trailing slashes. */
static inline int horizon_unix_path_equal( const char *a, const char *b )
{
    for (;;)
    {
        char ca, cb;

        while (a[0] == '/' && (a[1] == '/' || !a[1])) a++;
        while (b[0] == '/' && (b[1] == '/' || !b[1])) b++;
        ca = *a;
        cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        if (!ca) return 1;
        a++;
        b++;
    }
}
#endif
