/* Verify generic rights and real descriptor modes without Switch hardware. */
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winnt.h"
#include "winternl.h"
#include "../../dlls/ntdll/unix/horizon_file_access.h"

int main(void)
{
    char path[] = "/tmp/wine-nx-file-access-XXXXXX", buffer[4] = {0};
    int fd = mkstemp(path);
    unsigned int access = GENERIC_WRITE | SYNCHRONIZE | FILE_READ_ATTRIBUTES;
    assert(fd >= 0); close(fd);
    assert(horizon_file_map_access(GENERIC_READ) == FILE_GENERIC_READ);
    assert(horizon_file_map_access(GENERIC_WRITE) == FILE_GENERIC_WRITE);
    assert(horizon_file_map_access(GENERIC_EXECUTE) == FILE_GENERIC_EXECUTE);
    assert(horizon_file_map_access(GENERIC_ALL) == FILE_ALL_ACCESS);
    assert(horizon_file_map_access(access) == (FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES));
    assert(!(horizon_file_map_access(GENERIC_READ) & FILE_WRITE_DATA));
    assert(horizon_file_map_access(DELETE | FILE_WRITE_DATA) == (DELETE | FILE_WRITE_DATA));
    /* check_sharing, as wineserver decides it. Nothing open: anything goes. */
    assert(!horizon_file_sharing_violation(0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           FILE_GENERIC_WRITE, 0));
    /* The Sims 2: read sharing reads only, then a write probe. */
    assert(horizon_file_sharing_violation(FILE_GENERIC_READ, FILE_SHARE_READ,
                                          horizon_file_map_access(GENERIC_WRITE | SYNCHRONIZE), FILE_SHARE_READ));
    /* A second reader that shares reads is fine; one that refuses to share them is not. */
    assert(!horizon_file_sharing_violation(FILE_GENERIC_READ, FILE_SHARE_READ, FILE_GENERIC_READ, FILE_SHARE_READ));
    assert(horizon_file_sharing_violation(FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          FILE_GENERIC_READ, 0));
    /* Writers that share writes coexist; an attributes query ignores sharing. */
    assert(!horizon_file_sharing_violation(FILE_GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           FILE_GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE));
    assert(!horizon_file_sharing_violation(FILE_GENERIC_WRITE, 0, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0));
    /* DeleteFile on an open file needs FILE_SHARE_DELETE from it. */
    assert(horizon_file_sharing_violation(FILE_GENERIC_READ, FILE_SHARE_READ, DELETE, FILE_SHARE_DELETE));
    assert(!horizon_file_sharing_violation(FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                           DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE));
    assert((horizon_file_access_mode(access) & O_ACCMODE) == O_WRONLY);
    assert(!(horizon_file_access_mode(access) & O_APPEND));
    fd = open(path, horizon_file_access_mode(access) | O_TRUNC);
    assert(fd >= 0 && write(fd, "abc", 3) == 3);
    assert(lseek(fd, 1, SEEK_SET) == 1 && write(fd, "Z", 1) == 1);
    assert(fsync(fd) == 0 && close(fd) == 0);
    fd = open(path, horizon_file_access_mode(FILE_APPEND_DATA));
    assert(fd >= 0 && lseek(fd, 0, SEEK_SET) == 0 && write(fd, "!", 1) == 1);
    assert(close(fd) == 0);
    fd = open(path, horizon_file_access_mode(GENERIC_READ));
    assert(fd >= 0 && read(fd, buffer, 4) == 4);
    assert(buffer[0]=='a' && buffer[1]=='Z' && buffer[2]=='c' && buffer[3]=='!');
    assert(write(fd, "x", 1) == -1); /* Read-only access remains read-only. */
    assert(close(fd) == 0 && unlink(path) == 0);
    assert((horizon_file_access_mode(GENERIC_READ | GENERIC_WRITE) & O_ACCMODE) == O_RDWR);
    /* Opening an existing directory without FILE_DIRECTORY_FILE. */
    assert(horizon_directory_open_status(FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                         horizon_file_access_mode(FILE_READ_ATTRIBUTES)) == STATUS_SUCCESS);
    assert(horizon_directory_open_status(0, horizon_file_access_mode(GENERIC_WRITE) | O_CREAT) == STATUS_SUCCESS);
    assert(horizon_directory_open_status(FILE_NON_DIRECTORY_FILE, O_RDONLY) == (unsigned int)STATUS_FILE_IS_A_DIRECTORY);
    assert(horizon_directory_open_status(0, O_WRONLY | O_CREAT | O_EXCL) == (unsigned int)STATUS_OBJECT_NAME_COLLISION);
    assert(horizon_directory_open_status(0, O_WRONLY | O_CREAT | O_TRUNC) == (unsigned int)STATUS_OBJECT_NAME_COLLISION);
    /* Disposition: DeleteFile through SetFileInformationByHandle, and undoing it. */
    {
        unsigned int options = FILE_SYNCHRONOUS_IO_NONALERT;
        assert(horizon_disposition_update(&options, FILE_DISPOSITION_DELETE));
        assert(!horizon_disposition_update(&options, FILE_DISPOSITION_DO_NOT_DELETE));
        options = FILE_DELETE_ON_CLOSE;
        /* Without ON_CLOSE the open-time request stands... */
        assert(horizon_disposition_update(&options, FILE_DISPOSITION_DO_NOT_DELETE));
        assert(options == FILE_DELETE_ON_CLOSE);
        /* ...with it, it is withdrawn. */
        assert(!horizon_disposition_update(&options, FILE_DISPOSITION_DO_NOT_DELETE | FILE_DISPOSITION_ON_CLOSE));
        assert(!(options & FILE_DELETE_ON_CLOSE));
        assert(horizon_disposition_update(&options, FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS));
    }
    /* Rename targets (MoveFileExW, 7-Zip replacing an archive with its temporary). */
    {
        enum horizon_rename_action action;
        assert(horizon_rename_check(0, 0, 0, 0, 0, &action) == STATUS_SUCCESS && action == HORIZON_RENAME_MOVE);
        assert(horizon_rename_check(1, 1, 0, 1, 0, &action) == STATUS_SUCCESS && action == HORIZON_RENAME_NOTHING);
        assert(horizon_rename_check(1, 0, 0, 0, 0, &action) == (unsigned int)STATUS_OBJECT_NAME_COLLISION);
        assert(horizon_rename_check(1, 0, 0, 0, FILE_RENAME_REPLACE_IF_EXISTS, &action) == STATUS_SUCCESS &&
               action == HORIZON_RENAME_REPLACE);
        assert(horizon_rename_check(1, 0, 1, 0, FILE_RENAME_REPLACE_IF_EXISTS, &action) == (unsigned int)STATUS_ACCESS_DENIED);
        assert(horizon_rename_check(1, 0, 0, 1, FILE_RENAME_REPLACE_IF_EXISTS, &action) == (unsigned int)STATUS_ACCESS_DENIED);
    }
    /* FindFirstFileW on a missing name must say "file not found" (7-Zip printed
     * "No more files" on hardware), while FindNextFileW ends with NO_MORE_FILES. */
    assert(horizon_dir_scan_end_status(1) == (unsigned int)STATUS_NO_SUCH_FILE);
    assert(horizon_dir_scan_end_status(0) == (unsigned int)STATUS_NO_MORE_FILES);
    /* Open-file lookup: the server's name against a directory listing's. */
    assert(horizon_unix_path_equal("sdmc:/switch/wine/stdout.txt", "sdmc:/switch/wine/stdout.txt"));
    assert(horizon_unix_path_equal("sdmc:/switch/wine/drive_c/", "sdmc:/switch/wine/drive_c"));
    assert(horizon_unix_path_equal("sdmc:/switch/wine/drive_c//7zr-Tree/A.TXT",
                                   "SDMC:/switch/wine/drive_c/7zr-tree/a.txt"));
    assert(horizon_unix_path_equal("sdmc:/", "sdmc:"));
    assert(!horizon_unix_path_equal("sdmc:/switch/wine/ab", "sdmc:/switch/wine/a/b"));
    assert(!horizon_unix_path_equal("sdmc:/switch/wine/a", "sdmc:/switch/wine/ab"));
    assert(!horizon_unix_path_equal("sdmc:/switch/wine/a.txt", "sdmc:/switch/wine/a.txt2"));
    assert(!horizon_unix_path_equal("sdmc:/x/\xc3\xa9", "sdmc:/x/\xc3\x89")); /* only ASCII folds */
    /* Directory entry names, as the Horizon server returns them with the
     * directory's unix name (which carries a trailing slash for C:\). */
    {
        static const char *const cases[][3] =
        {
            { "sdmc:/switch/wine/drive_c/", "7zr-tree", "sdmc:/switch/wine/drive_c/7zr-tree" },
            { "sdmc:/switch/wine/drive_c/7zr-tree", "data", "sdmc:/switch/wine/drive_c/7zr-tree/data" },
            { "sdmc:/switch/wine/drive_c/", ".", "sdmc:/switch/wine/drive_c" },
            { "sdmc:/switch/wine/drive_c/", "..", "sdmc:/switch/wine" },
            { "sdmc:/switch//", "..", "sdmc:/" },
            { "sdmc:/", "x.txt", "sdmc:/x.txt" },
            { "sdmc:/", ".", "sdmc:/" },
            { "sdmc:/", "..", "sdmc:/" },
            { "sdmc:/a", "..", "sdmc:/" },
            { "sdmc:/a", ".hidden", "sdmc:/a/.hidden" },
            { "sdmc:/a", "...", "sdmc:/a/..." },
        };
        unsigned int i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            char *path = horizon_dir_entry_path(cases[i][0], cases[i][1]);
            if (strcmp(path, cases[i][2]))
                fprintf(stderr, "entry %s + %s -> %s, expected %s\n", cases[i][0], cases[i][1], path, cases[i][2]);
            assert(!strcmp(path, cases[i][2]));
            free(path);
        }
    }
    puts("Horizon file access: generic mappings, write/seek, append-only, read-only, directory opens, disposition, rename checks, scan end status, path matching and entry names passed");
    return 0;
}
