#include "deploy.h"
#include <string.h>
#include <stdio.h>

#define BAD_BUNDLE MAKERESULT(Module_HomebrewLoader, 100)
#define NO_SPACE MAKERESULT(Module_HomebrewLoader, 101)
#define BAD_HASH MAKERESULT(Module_HomebrewLoader, 102)
#define IO_ERROR MAKERESULT(Module_HomebrewLoader, 103)
#define CHUNK 65536
#define MAX_BUNDLE (32ull * 1024 * 1024 * 1024)
#define MAX_FILES 16384
#define MAX_FILE_SIZE 0xffffffffull
/* Explicit fixed wire layout. Both host builder and Switch are little-endian. */
typedef struct { char magic[8]; u32 version, count; char game_id[32]; } Header;
typedef struct { char path[192]; u64 offset, size; u8 hash[32]; } Entry;
_Static_assert(sizeof(Header) == 48, "header layout");
_Static_assert(sizeof(Entry) == 240, "entry layout");
static u8 buffer[CHUNK];
static Header header;
static Entry entries[MAX_FILES];
static bool valid[MAX_FILES];
static bool part_ready[MAX_FILES];
static RuntimeDeployProgress progress_callback;
static void *progress_context;
static u64 progress_done, progress_total;
static Result directory(FsFileSystem *fs, const char *path);

/* Mutable defaults are intentionally absent from the immutable bundle. */
static const char settings_seed[] =
    "{\n"
    "  \"run-the-chosen-program\": true,\n"
    "  \"verbose-log\": false,\n"
    "  \"profiler\": false,\n"
    "  \"core-balancing\": true,\n"
    "  \"display-devices\": true,\n"
    "  \"windows-through-opengl\": true,\n"
    "  \"vulkan-probe\": false,\n"
    "  \"reopen-the-launcher-on-exit\": false,\n"
    "  \"hand-the-process-back-anyway\": false,\n"
    "  \"gl-pinned-buffers-cached\": true,\n"
    "  \"gl-clean-before-submit\": true,\n"
    "  \"gl-clean-test\": false,\n"
    "  \"keyboard-on-text-focus\": true,\n"
    "  \"dxvk-for-new-games\": true\n"
    "}\n";
static const char game_seed[] =
    "verbose=0\n"
    "profile=1\n"
    "title=WA2 Full Menu ARGB Test\n"
    "d3d=dxvk\n";

void runtime_deploy_set_progress(RuntimeDeployProgress callback, void *context) {
    progress_callback = callback;
    progress_context = context;
}
static void progress(RuntimeDeployPhase phase, const char *path) {
    if (progress_callback) progress_callback(phase, progress_done, progress_total, path, progress_context);
}
static unsigned char fold(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}
static int path_compare(const char *a, const char *b) {
    while (*a && *b) {
        int d = (int)fold((unsigned char)*a) - (int)fold((unsigned char)*b);
        if (d) return d;
        a++; b++;
    }
    return (int)fold((unsigned char)*a) - (int)fold((unsigned char)*b);
}
static bool path_starts(const char *path, const char *prefix) {
    while (*prefix) if (fold((unsigned char)*path++) != fold((unsigned char)*prefix++)) return false;
    return true;
}
static bool parent_conflict(const char *parent, const char *child) {
    while (*parent && *child && fold((unsigned char)*parent) == fold((unsigned char)*child)) {
        parent++; child++;
    }
    return !*parent && *child == '/';
}
static bool reserved(const char *path) {
    return !path_compare(path, "ready.sha256") || !path_compare(path, "verify-all.flag") ||
           !path_compare(path, "user.reg") || !path_compare(path, "system.reg") ||
           !path_compare(path, "userdef.reg") || !path_compare(path, "config/user.reg") ||
           !path_compare(path, "config/system.reg") || !path_compare(path, "config/userdef.reg") ||
           path_starts(path, "drive_c/users/") || path_starts(path, "registry/") ||
           path_starts(path, "logs/") ||
           !path_compare(path, "drive_c/WA2/WA2_full_menu.wine-nx.txt") ||
           (path_starts(path, "config/") && path_compare(path, "config/classes.reg"));
}
static bool ancestor_exists(const char *path, u32 count) {
    char prefix[192];
    size_t len = strlen(path);
    for (size_t n = 0; n < len; n++) if (path[n] == '/') {
        memcpy(prefix, path, n); prefix[n] = 0;
        u32 low = 1, high = count;
        while (low < high) {
            u32 middle = low + (high - low) / 2;
            int comparison = path_compare(entries[middle].path, prefix);
            if (!comparison) return true;
            if (comparison < 0) low = middle + 1;
            else high = middle;
        }
        if (!path_compare(entries[0].path, prefix)) return true;
    }
    return false;
}

static bool valid_path(const char *path) {
    const char *end = memchr(path, 0, 192);
    if (!end) return false;
    size_t len = end - path;
    if (!len || path[0] == '/' || path[len-1] == '/') return false;
    size_t segment = 0;
    for (size_t i = 0; i <= len; i++) {
        char c = path[i];
        if (c == '/' || c == 0) {
            size_t n = i - segment;
            if (!n || (n == 1 && path[segment] == '.') ||
                (n == 2 && path[segment] == '.' && path[segment+1] == '.')) return false;
            segment = i + 1;
        } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    if (len >= 4 && !path_compare(path + len - 4, ".pak")) return false;
    return !reserved(path) &&
           (len < 5 || path_compare(path + len - 5, ".part"));
}

static Result parent_directories(FsFileSystem *fs, const char *relative) {
    char path[sizeof(RUNTIME_ROOT) + 192];
    snprintf(path, sizeof(path), "%s/%.191s", RUNTIME_ROOT, relative);
    for (char *p = path + strlen(RUNTIME_ROOT) + 1; *p; p++) if (*p == '/') {
        *p = 0;
        Result rc = directory(fs, path);
        *p = '/';
        if (R_FAILED(rc)) return rc;
    }
    return 0;
}

static Result directory(FsFileSystem *fs, const char *path) {
    FsDirEntryType type;
    Result rc = fsFsGetEntryType(fs, path, &type);
    if (R_SUCCEEDED(rc)) return type == FsDirEntryType_Dir ? 0 : IO_ERROR;
    return fsFsCreateDirectory(fs, path);
}
static bool file_check(FsFileSystem *fs, const char *path, u64 length, const u8 *expected,
                       bool hash_contents, bool report, RuntimeDeployPhase phase) {
    FsFile file;
    if (R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Read, &file))) return false;
    s64 size;
    bool ok = R_SUCCEEDED(fsFileGetSize(&file, &size)) && size >= 0 && (u64)size == length;
    if (!ok || !hash_contents) { fsFileClose(&file); return ok; }
    Sha256Context hash;
    sha256ContextCreate(&hash);
    for (u64 pos = 0; ok && pos < length;) {
        u64 got = 0;
        size_t n = length - pos > CHUNK ? CHUNK : length - pos;
        ok = R_SUCCEEDED(fsFileRead(&file, pos, buffer, n, FsReadOption_None, &got)) && got == n;
        if (ok) sha256ContextUpdate(&hash, buffer, n);
        pos += n;
        if (report) { progress_done += n; progress(phase, path); }
    }
    u8 digest[32];
    sha256ContextGetHash(&hash, digest);
    fsFileClose(&file);
    return ok && !memcmp(digest, expected, 32);
}
static bool game_asset(const char *path) {
    return !strncmp(path, "drive_c/WA2/", sizeof("drive_c/WA2/") - 1);
}
static Result remove_if_present(FsFileSystem *fs, const char *path) {
    FsDirEntryType type;
    Result rc = fsFsGetEntryType(fs, path, &type);
    if (R_FAILED(rc)) return rc == MAKERESULT(2, 1) ? 0 : rc;
    if (type != FsDirEntryType_File) return IO_ERROR;
    return fsFsDeleteFile(fs, path);
}
static Result seed_if_missing(FsFileSystem *fs, const char *relative,
                              const char *contents, size_t length) {
    char path[sizeof(RUNTIME_ROOT) + 192], temporary[sizeof(path) + 5];
    snprintf(path, sizeof(path), "%s/%s", RUNTIME_ROOT, relative);
    snprintf(temporary, sizeof(temporary), "%s.part", path);
    FsDirEntryType type;
    Result rc = fsFsGetEntryType(fs, path, &type);
    if (R_SUCCEEDED(rc)) return type == FsDirEntryType_File ? 0 : IO_ERROR;
    if (rc != MAKERESULT(2, 1)) return rc;
    rc = parent_directories(fs, relative);
    if (R_FAILED(rc)) return rc;
    rc = remove_if_present(fs, temporary);
    if (R_FAILED(rc)) return rc;
    rc = fsFsCreateFile(fs, temporary, length, 0);
    if (R_FAILED(rc)) return rc;
    FsFile file;
    rc = fsFsOpenFile(fs, temporary, FsOpenMode_Write, &file);
    if (R_FAILED(rc)) return rc;
    rc = fsFileWrite(&file, 0, contents, length, FsWriteOption_None);
    if (R_SUCCEEDED(rc)) rc = fsFileFlush(&file);
    fsFileClose(&file);
    if (R_FAILED(rc)) return rc;
    rc = fsFsRenameFile(fs, temporary, path);
    if (R_SUCCEEDED(rc)) rc = fsFsCommit(fs);
    return rc;
}
static Result copy_entry(FsStorage *storage, u64 base, FsFileSystem *fs, const Entry *e,
                         bool ready_part) {
    char path[sizeof(RUNTIME_ROOT) + 192], temporary[sizeof(path) + 5];
    snprintf(path, sizeof(path), "%s/%.191s", RUNTIME_ROOT, e->path);
    snprintf(temporary, sizeof(temporary), "%s.part", path);
    /* A fully verified part can be promoted after an interrupted deployment. */
    if (ready_part) {
        Result rc = remove_if_present(fs, path);
        if (R_FAILED(rc)) return rc;
        rc = fsFsRenameFile(fs, temporary, path);
        if (R_SUCCEEDED(rc)) rc = fsFsCommit(fs);
        progress_done += e->size;
        progress(RuntimeDeploy_Copying, e->path);
        return rc;
    }
    Result rc = remove_if_present(fs, temporary);
    if (R_FAILED(rc)) return rc;
    rc = fsFsCreateFile(fs, temporary, e->size, 0);
    if (R_FAILED(rc)) return rc;
    FsFile file;
    rc = fsFsOpenFile(fs, temporary, FsOpenMode_Write, &file);
    if (R_FAILED(rc)) return rc;
    Sha256Context hash;
    sha256ContextCreate(&hash);
    for (u64 pos = 0; pos < e->size; ) {
        size_t n = e->size - pos > CHUNK ? CHUNK : e->size - pos;
        rc = fsStorageRead(storage, base + e->offset + pos, buffer, n);
        if (R_FAILED(rc)) break;
        sha256ContextUpdate(&hash, buffer, n);
        rc = fsFileWrite(&file, pos, buffer, n, FsWriteOption_None);
        if (R_FAILED(rc)) break;
        pos += n;
        progress_done += n;
        progress(RuntimeDeploy_Copying, e->path);
    }
    if (R_SUCCEEDED(rc)) rc = fsFileFlush(&file);
    fsFileClose(&file);
    if (R_FAILED(rc)) return rc;
    u8 digest[32];
    sha256ContextGetHash(&hash, digest);
    if (memcmp(digest, e->hash, 32)) return BAD_HASH;
    /* Read back before replacement. A failed copy never marks deployment ready. */
    u64 copied_done = progress_done, copied_total = progress_total;
    progress_done = 0; progress_total = e->size;
    progress(RuntimeDeploy_Verifying, e->path);
    bool verified = file_check(fs, temporary, e->size, e->hash, true, true, RuntimeDeploy_Verifying);
    progress_done = copied_done; progress_total = copied_total;
    if (!verified) return BAD_HASH;
    rc = remove_if_present(fs, path);
    if (R_FAILED(rc)) return rc;
    rc = fsFsRenameFile(fs, temporary, path);
    if (R_SUCCEEDED(rc)) rc = fsFsCommit(fs);
    return rc;
}
Result runtime_deploy(FsStorage *storage, u64 offset, u64 size, bool *changed) {
    *changed = false;
    if (size < sizeof(header) + sizeof(Entry) || size > MAX_BUNDLE || offset > INT64_MAX - size) return BAD_BUNDLE;
    Result rc = fsStorageRead(storage, offset, &header, sizeof(header));
    if (R_FAILED(rc)) return rc;
    if (memcmp(header.magic, "ARPBNDL2", 8) || header.version != 2 || !header.count ||
        header.count > MAX_FILES || memcmp(header.game_id, RUNTIME_ID, sizeof(RUNTIME_ID))) return BAD_BUNDLE;
    u64 table_size = (u64)header.count * sizeof(Entry);
    if (table_size > size - sizeof(header)) return BAD_BUNDLE;
    rc = fsStorageRead(storage, offset + sizeof(header), entries, table_size);
    if (R_FAILED(rc)) return rc;
    u64 end = sizeof(header) + table_size;
    bool has_exe = false;
    for (u32 i = 0; i < header.count; i++) {
        Entry *e = &entries[i];
        if (!valid_path(e->path) || e->offset != end || e->size > MAX_FILE_SIZE ||
            e->size > size - end) return BAD_BUNDLE;
        end += e->size;
        if (i == 0 ? strcmp(e->path, "wine-nx-runtime.nro") :
            (!path_compare(e->path, "wine-nx-runtime.nro") ||
             (i > 1 && (path_compare(entries[i-1].path, e->path) >= 0 ||
                        parent_conflict(entries[i-1].path, e->path))))) return BAD_BUNDLE;
        if (!strcmp(e->path, "drive_c/WA2/WA2_full_menu.exe")) has_exe = true;
    }
    if (end != size || !has_exe) return BAD_BUNDLE;
    for (u32 i = 0; i < header.count; i++)
        if (ancestor_exists(entries[i].path, header.count)) return BAD_BUNDLE;
    u8 marker[32];
    Sha256Context manifest_hash;
    sha256ContextCreate(&manifest_hash);
    sha256ContextUpdate(&manifest_hash, &header, sizeof(header));
    sha256ContextUpdate(&manifest_hash, entries, table_size);
    sha256ContextGetHash(&manifest_hash, marker);
    FsFileSystem fs;
    rc = fsOpenSdCardFileSystem(&fs);
    if (R_FAILED(rc)) return rc;
    rc = directory(&fs, "/switch");
    if (R_SUCCEEDED(rc)) rc = directory(&fs, "/switch/autorun-games");
    if (R_SUCCEEDED(rc)) rc = directory(&fs, RUNTIME_ROOT);
    if (R_FAILED(rc)) goto done;
    u8 current_marker[32];
    FsFile marker_file;
    bool marker_valid = false;
    if (R_SUCCEEDED(fsFsOpenFile(&fs, RUNTIME_ROOT "/ready.sha256", FsOpenMode_Read, &marker_file))) {
        s64 marker_size;
        u64 got = 0;
        marker_valid = R_SUCCEEDED(fsFileGetSize(&marker_file, &marker_size)) && marker_size == 32 &&
                       R_SUCCEEDED(fsFileRead(&marker_file, 0, current_marker, 32,
                                              FsReadOption_None, &got)) && got == 32 &&
                       !memcmp(marker, current_marker, 32);
        fsFileClose(&marker_file);
    }
    FsDirEntryType flag_type;
    bool force_verify = R_SUCCEEDED(fsFsGetEntryType(&fs, RUNTIME_ROOT "/verify-all.flag", &flag_type)) &&
                        flag_type == FsDirEntryType_File;
    u64 required = 1024 * 1024;
    bool any_invalid = false;
    progress_total = 0;
    progress_done = 0;
    for (u32 i = 0; i < header.count; i++) if (force_verify || !marker_valid || !game_asset(entries[i].path))
        progress_total += entries[i].size;
    progress(RuntimeDeploy_Validating, NULL);
    for (u32 i = 0; i < header.count; i++) {
        char path[sizeof(RUNTIME_ROOT) + 192];
        snprintf(path, sizeof(path), "%s/%.191s", RUNTIME_ROOT, entries[i].path);
        bool need_hash = force_verify || !marker_valid || !game_asset(entries[i].path);
        valid[i] = file_check(&fs, path, entries[i].size, entries[i].hash, need_hash,
                              need_hash, RuntimeDeploy_Validating);
        part_ready[i] = false;
        if (!valid[i]) {
            char part[sizeof(RUNTIME_ROOT) + 192 + 5];
            snprintf(part, sizeof(part), "%s/%.191s.part", RUNTIME_ROOT, entries[i].path);
            u64 validation_done = progress_done, validation_total = progress_total;
            progress_done = 0; progress_total = entries[i].size;
            progress(RuntimeDeploy_Verifying, entries[i].path);
            part_ready[i] = file_check(&fs, part, entries[i].size, entries[i].hash,
                                        true, true, RuntimeDeploy_Verifying);
            progress_done = validation_done; progress_total = validation_total;
            if (!part_ready[i]) {
                required += entries[i].size;
            }
            any_invalid = true;
        }
        if (valid[i]) {
            char part[sizeof(RUNTIME_ROOT) + 192 + 5];
            snprintf(part, sizeof(part), "%s/%.191s.part", RUNTIME_ROOT, entries[i].path);
            rc = remove_if_present(&fs, part);
            if (R_FAILED(rc)) goto done;
        }
        progress(RuntimeDeploy_Validating, entries[i].path);
    }
    if (any_invalid) {
        rc = remove_if_present(&fs, RUNTIME_ROOT "/ready.sha256");
        if (R_FAILED(rc)) goto done;
        rc = fsFsCommit(&fs);
        if (R_FAILED(rc)) goto done;
        for (u32 i = 0; i < header.count; i++) if (!valid[i] && !part_ready[i]) {
            char part[sizeof(RUNTIME_ROOT) + 192 + 5];
            snprintf(part, sizeof(part), "%s/%.191s.part", RUNTIME_ROOT, entries[i].path);
            rc = remove_if_present(&fs, part);
            if (R_FAILED(rc)) goto done;
        }
        s64 free_bytes;
        rc = fsFsGetFreeSpace(&fs, RUNTIME_ROOT, &free_bytes);
        if (R_FAILED(rc)) goto done;
        if (free_bytes < 0 || (u64)free_bytes < required) { rc = NO_SPACE; goto done; }
        progress_done = 0;
        progress_total = required - 1024 * 1024;
        for (u32 i = 0; i < header.count; i++) if (!valid[i] && part_ready[i])
            progress_total += entries[i].size;
        progress(RuntimeDeploy_Copying, NULL);
        for (u32 i = 0; i < header.count; i++) if (!valid[i]) {
            rc = parent_directories(&fs, entries[i].path);
            if (R_FAILED(rc)) goto done;
            rc = copy_entry(storage, offset, &fs, &entries[i], part_ready[i]);
            if (R_FAILED(rc)) goto done;
            *changed = true;
        }
    }
    rc = seed_if_missing(&fs, "config/settings.json", settings_seed, sizeof(settings_seed) - 1);
    if (R_FAILED(rc)) goto done;
    rc = seed_if_missing(&fs, "drive_c/WA2/WA2_full_menu.wine-nx.txt",
                         game_seed, sizeof(game_seed) - 1);
    if (R_FAILED(rc)) goto done;
    if (!marker_valid || any_invalid) {
        rc = remove_if_present(&fs, RUNTIME_ROOT "/ready.sha256");
        if (R_FAILED(rc)) goto done;
        rc = fsFsCreateFile(&fs, RUNTIME_ROOT "/ready.sha256", 32, 0);
        if (R_FAILED(rc)) goto done;
        FsFile f;
        rc = fsFsOpenFile(&fs, RUNTIME_ROOT "/ready.sha256", FsOpenMode_Write, &f);
        if (R_FAILED(rc)) goto done;
        rc = fsFileWrite(&f, 0, marker, 32, FsWriteOption_Flush);
        fsFileClose(&f);
        if (R_SUCCEEDED(rc)) rc = fsFsCommit(&fs);
    }
    if (R_SUCCEEDED(rc) && force_verify) {
        rc = fsFsDeleteFile(&fs, RUNTIME_ROOT "/verify-all.flag");
        if (R_SUCCEEDED(rc)) rc = fsFsCommit(&fs);
    }
done:
    fsFsClose(&fs);
    return rc;
}
