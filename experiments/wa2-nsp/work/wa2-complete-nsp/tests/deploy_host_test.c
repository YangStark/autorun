#define _POSIX_C_SOURCE 200809L
#include "switch.h"
#include "../loader/deploy.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Link to the system OpenSSL shared library without needing its headers. */
extern void *EVP_MD_CTX_new(void);
extern void EVP_MD_CTX_free(void *);
extern const void *EVP_sha256(void);
extern int EVP_DigestInit_ex(void *, const void *, void *);
extern int EVP_DigestUpdate(void *, const void *, size_t);
extern int EVP_DigestFinal_ex(void *, unsigned char *, unsigned int *);

static char sd_root[PATH_MAX];
static s64 available_bytes = INT64_MAX / 2;
static int fail_write_number, payload_writes, total_writes, renames, commits;
static u64 game_bytes_read;
static int checks;
#define ERROR 7000
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static void host_path(char out[PATH_MAX], const char *path) {
    CHECK(path[0] == '/' && !strstr(path, ".."));
    CHECK(strlen(sd_root) + strlen(path) < PATH_MAX);
    strcpy(out, sd_root);
    strcat(out, path);
}
Result fsStorageRead(FsStorage *s, u64 offset, void *out, size_t size) {
    if (offset > s->length || size > s->length - offset) return ERROR;
    memcpy(out, s->bytes + offset, size);
    return 0;
}
Result fsOpenSdCardFileSystem(FsFileSystem *fs) { (void)fs; return 0; }
void fsFsClose(FsFileSystem *fs) { (void)fs; }
Result fsFsGetEntryType(FsFileSystem *fs, const char *path, FsDirEntryType *type) {
    (void)fs; char p[PATH_MAX]; struct stat st; host_path(p, path);
    if (stat(p, &st)) return errno == ENOENT ? MAKERESULT(2, 1) : ERROR;
    *type = S_ISDIR(st.st_mode) ? FsDirEntryType_Dir : FsDirEntryType_File;
    return 0;
}
Result fsFsCreateDirectory(FsFileSystem *fs, const char *path) {
    (void)fs; char p[PATH_MAX]; host_path(p, path); return mkdir(p, 0700) ? ERROR : 0;
}
Result fsFsCreateFile(FsFileSystem *fs, const char *path, s64 size, u32 flags) {
    (void)fs; (void)flags; char p[PATH_MAX]; host_path(p, path);
    int fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return ERROR;
    int ok = size >= 0 && ftruncate(fd, size) == 0;
    close(fd); return ok ? 0 : ERROR;
}
Result fsFsDeleteFile(FsFileSystem *fs, const char *path) {
    (void)fs; char p[PATH_MAX]; host_path(p, path); return unlink(p) ? ERROR : 0;
}
Result fsFsOpenFile(FsFileSystem *fs, const char *path, u32 mode, FsFile *file) {
    (void)fs; char p[PATH_MAX]; host_path(p, path);
    file->fd = open(p, mode == FsOpenMode_Read ? O_RDONLY : O_WRONLY);
    file->game_asset = strstr(path, "/drive_c/WA2/") != NULL;
    return file->fd < 0 ? ERROR : 0;
}
Result fsFsRenameFile(FsFileSystem *fs, const char *oldpath, const char *newpath) {
    (void)fs; char a[PATH_MAX], b[PATH_MAX]; host_path(a, oldpath); host_path(b, newpath);
    if (rename(a, b)) return ERROR;
    renames++; return 0;
}
Result fsFsCommit(FsFileSystem *fs) { (void)fs; commits++; return 0; }
Result fsFsGetFreeSpace(FsFileSystem *fs, const char *path, s64 *free_bytes) {
    (void)fs; (void)path; *free_bytes = available_bytes; return 0;
}
Result fsFileGetSize(FsFile *f, s64 *size) {
    struct stat st; if (fstat(f->fd, &st)) return ERROR; *size = st.st_size; return 0;
}
Result fsFileRead(FsFile *f, u64 offset, void *out, size_t size, u32 options, u64 *read_size) {
    (void)options; ssize_t got = pread(f->fd, out, size, (off_t)offset);
    if (got < 0) return ERROR;
    if (f->game_asset) game_bytes_read += got;
    *read_size = got;
    return 0;
}
Result fsFileWrite(FsFile *f, u64 offset, const void *data, size_t size, u32 options) {
    total_writes++;
    if (size != 32) payload_writes++;
    if (fail_write_number && payload_writes == fail_write_number) return ERROR;
    ssize_t wrote = pwrite(f->fd, data, size, (off_t)offset);
    if (wrote != (ssize_t)size) return ERROR;
    return options == FsWriteOption_Flush && fsync(f->fd) ? ERROR : 0;
}
Result fsFileFlush(FsFile *f) { return fsync(f->fd) ? ERROR : 0; }
void fsFileClose(FsFile *f) { close(f->fd); }
void sha256ContextCreate(Sha256Context *ctx) {
    ctx->evp = EVP_MD_CTX_new(); CHECK(ctx->evp && EVP_DigestInit_ex(ctx->evp, EVP_sha256(), NULL) == 1);
}
void sha256ContextUpdate(Sha256Context *ctx, const void *data, size_t size) {
    CHECK(EVP_DigestUpdate(ctx->evp, data, size) == 1);
}
void sha256ContextGetHash(Sha256Context *ctx, u8 hash[32]) {
    unsigned int length = 0;
    CHECK(EVP_DigestFinal_ex(ctx->evp, hash, &length) == 1 && length == 32);
    EVP_MD_CTX_free(ctx->evp); ctx->evp = NULL;
}
void sha256CalculateHash(u8 hash[32], const void *data, size_t size) {
    Sha256Context ctx; sha256ContextCreate(&ctx); sha256ContextUpdate(&ctx, data, size);
    sha256ContextGetHash(&ctx, hash);
}

static const char *root = RUNTIME_ROOT;
static void path_for(char out[PATH_MAX], const char *name) {
    char logical[256]; snprintf(logical, sizeof(logical), "%s/%s", root, name); host_path(out, logical);
}
static int exists(const char *name) {
    char p[PATH_MAX]; path_for(p, name); return access(p, F_OK) == 0;
}
static void read_exact(const char *name, u8 *out, size_t size) {
    char p[PATH_MAX]; path_for(p, name); int fd = open(p, O_RDONLY); CHECK(fd >= 0);
    CHECK(read(fd, out, size) == (ssize_t)size); close(fd);
}
static void write_exact(const char *name, const void *data, size_t size) {
    char p[PATH_MAX]; path_for(p, name); int fd = open(p, O_WRONLY | O_TRUNC);
    CHECK(fd >= 0); CHECK(write(fd, data, size) == (ssize_t)size); close(fd);
}
static void check_text(const char *name, const char *expected) {
    char p[PATH_MAX]; path_for(p, name);
    struct stat st; CHECK(stat(p, &st) == 0 && st.st_size == (off_t)strlen(expected));
    char *actual = malloc(st.st_size); CHECK(actual);
    read_exact(name, (u8 *)actual, st.st_size);
    CHECK(memcmp(actual, expected, st.st_size) == 0);
    free(actual);
}
static void seed_cases(FsStorage *storage, size_t length) {
    static const char settings[] =
        "{\n  \"run-the-chosen-program\": true,\n  \"verbose-log\": false,\n"
        "  \"profiler\": false,\n  \"core-balancing\": true,\n"
        "  \"display-devices\": true,\n  \"windows-through-opengl\": true,\n"
        "  \"vulkan-probe\": false,\n  \"reopen-the-launcher-on-exit\": false,\n"
        "  \"hand-the-process-back-anyway\": false,\n"
        "  \"gl-pinned-buffers-cached\": true,\n  \"gl-clean-before-submit\": true,\n"
        "  \"gl-clean-test\": false,\n  \"keyboard-on-text-focus\": true,\n"
        "  \"dxvk-for-new-games\": true\n}\n";
    static const char game[] =
        "verbose=0\nprofile=1\ntitle=WA2 Full Menu ARGB Test\nd3d=dxvk\n";
    check_text("config/settings.json", settings);
    check_text("drive_c/WA2/WA2_full_menu.wine-nx.txt", game);
    const char *custom = "player edits persist";
    char p[PATH_MAX]; path_for(p, "config/settings.json");
    CHECK(truncate(p, strlen(custom)) == 0);
    write_exact("config/settings.json", custom, strlen(custom));
    path_for(p, "drive_c/WA2/WA2_full_menu.wine-nx.txt");
    CHECK(truncate(p, strlen(custom)) == 0);
    write_exact("drive_c/WA2/WA2_full_menu.wine-nx.txt", custom, strlen(custom));
    int writes = total_writes, old_renames = renames;
    bool changed = true;
    CHECK(runtime_deploy(storage, 0, length, &changed) == 0 && !changed);
    CHECK(total_writes == writes && renames == old_renames);
    check_text("config/settings.json", custom);
    check_text("drive_c/WA2/WA2_full_menu.wine-nx.txt", custom);
    path_for(p, "config/settings.json"); CHECK(unlink(p) == 0);
    CHECK(runtime_deploy(storage, 0, length, &changed) == 0 && !changed);
    check_text("config/settings.json", settings);
    check_text("drive_c/WA2/WA2_full_menu.wine-nx.txt", custom);
    path_for(p, "drive_c/WA2/WA2_full_menu.wine-nx.txt"); CHECK(unlink(p) == 0);
    payload_writes = 0; fail_write_number = 1;
    CHECK(runtime_deploy(storage, 0, length, &changed) != 0 && !changed);
    CHECK(!exists("drive_c/WA2/WA2_full_menu.wine-nx.txt") &&
          exists("drive_c/WA2/WA2_full_menu.wine-nx.txt.part"));
    fail_write_number = 0;
    CHECK(runtime_deploy(storage, 0, length, &changed) == 0 && !changed);
    check_text("drive_c/WA2/WA2_full_menu.wine-nx.txt", game);
    CHECK(!exists("drive_c/WA2/WA2_full_menu.wine-nx.txt.part"));
    printf("PASS seed creation, preservation, and interrupted retry\n");
}
static u8 *load_bundle(size_t *size) {
    static const char *names[] = { "wine-nx-runtime.nro", "drive_c/WA2/WA2_full_menu.exe" };
    static const char *contents[] = { "test NRO payload", "test EXE payload" };
    *size = 48 + 2 * 240 + strlen(contents[0]) + strlen(contents[1]);
    u8 *bytes = calloc(1, *size); CHECK(bytes);
    memcpy(bytes, "ARPBNDL2", 8);
    u32 version = 2, count = 2;
    memcpy(bytes + 8, &version, 4); memcpy(bytes + 12, &count, 4);
    memcpy(bytes + 16, RUNTIME_ID, strlen(RUNTIME_ID));
    size_t cursor = 48 + 2 * 240;
    for (int i = 0; i < 2; i++) {
        u8 *entry = bytes + 48 + i * 240;
        size_t n = strlen(contents[i]);
        memcpy(entry, names[i], strlen(names[i]));
        memcpy(entry + 192, &cursor, 8); memcpy(entry + 200, &n, 8);
        memcpy(bytes + cursor, contents[i], n);
        sha256CalculateHash(entry + 208, bytes + cursor, n);
        cursor += n;
    }
    return bytes;
}
static u64 get_u64(const u8 *p) { u64 n; memcpy(&n, p, 8); return n; }
static void put_u64(u8 *p, u64 n) { memcpy(p, &n, 8); }
static void expect_bad(const u8 *bundle, size_t length, const char *label) {
    FsStorage storage = { bundle, length }; bool changed = true;
    int before = total_writes;
    Result rc = runtime_deploy(&storage, 0, length, &changed);
    if (rc == 0 || changed || total_writes != before) {
        fprintf(stderr, "Bad bundle accepted: %s (rc=%d)\n", label, rc); exit(1);
    }
    CHECK(exists("ready.sha256"));
}
static void mutate_cases(const u8 *good, size_t length) {
    u8 *copy = malloc(length); CHECK(copy);
    memcpy(copy, good, length); copy[48] = '/'; expect_bad(copy, length, "absolute path");
    memcpy(copy, good, length); memcpy(copy + 48, "../bad.nro", 11);
    expect_bad(copy, length, "traversal path");
    memcpy(copy, good, length); memset(copy + 48, 'x', 192);
    expect_bad(copy, length, "unterminated path");
    memcpy(copy, good, length); put_u64(copy + 48 + 192, UINT64_MAX);
    expect_bad(copy, length, "offset overflow");
    memcpy(copy, good, length); put_u64(copy + 48 + 192 + 8, UINT64_MAX);
    expect_bad(copy, length, "size overflow");
    memcpy(copy, good, length); copy[48 + 192 + 16] ^= 1;
    /* A false digest must fail before replacing the previously valid NRO. */
    FsStorage storage = { copy, length }; bool changed = false;
    CHECK(runtime_deploy(&storage, 0, length, &changed) != 0 && !changed);
    CHECK(!exists("ready.sha256") && exists("wine-nx-runtime.nro.part"));
    size_t probe_offset = (size_t)get_u64(good + 48 + 192);
    size_t probe_size = (size_t)get_u64(good + 48 + 200);
    u8 *installed = malloc(probe_size); CHECK(installed);
    read_exact("wine-nx-runtime.nro", installed, probe_size);
    CHECK(memcmp(installed, good + probe_offset, probe_size) == 0);
    free(installed);
    FsStorage original = { good, length };
    CHECK(runtime_deploy(&original, 0, length, &changed) == 0 && !changed);
    CHECK(exists("ready.sha256") && !exists("wine-nx-runtime.nro.part"));
    free(copy);
}

static u8 *game_bundle(const u8 *basic, size_t *length) {
    static const char asset[] = "a bundled game asset whose same-size damage needs full verification";
    size_t nro_size = get_u64(basic + 48 + 200);
    size_t exe_size = get_u64(basic + 288 + 200);
    size_t asset_size = sizeof(asset) - 1;
    *length = 48 + 3 * 240 + nro_size + asset_size + exe_size;
    u8 *bundle = calloc(1, *length); CHECK(bundle);
    memcpy(bundle, basic, 48);
    u32 count = 3; memcpy(bundle + 12, &count, 4);
    const char *names[] = { "wine-nx-runtime.nro", "drive_c/WA2/data.bin", "drive_c/WA2/WA2_full_menu.exe" };
    const u8 *data[] = { basic + get_u64(basic + 48 + 192), (const u8 *)asset,
                         basic + get_u64(basic + 288 + 192) };
    size_t lengths[] = { nro_size, asset_size, exe_size };
    size_t cursor = 48 + 3 * 240;
    for (int i = 0; i < 3; i++) {
        u8 *entry = bundle + 48 + i * 240;
        memcpy(entry, names[i], strlen(names[i]));
        memcpy(entry + 192, &cursor, 8); memcpy(entry + 200, &lengths[i], 8);
        memcpy(bundle + cursor, data[i], lengths[i]);
        sha256CalculateHash(entry + 208, bundle + cursor, lengths[i]);
        cursor += lengths[i];
    }
    return bundle;
}
static void game_cache_cases(const u8 *basic) {
    size_t length; u8 *bundle = game_bundle(basic, &length);
    FsStorage storage = { bundle, length }; bool changed = false;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    game_bytes_read = 0; changed = true;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && !changed && game_bytes_read == 0);
    u8 wrong = 'X'; char game_path[PATH_MAX]; path_for(game_path, "drive_c/WA2/data.bin");
    int damage_fd = open(game_path, O_WRONLY); CHECK(damage_fd >= 0);
    CHECK(pwrite(damage_fd, &wrong, 1, 0) == 1); close(damage_fd);
    game_bytes_read = 0; changed = true;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && !changed && game_bytes_read == 0);
    char flag[PATH_MAX]; path_for(flag, "verify-all.flag");
    int fd = open(flag, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(fd >= 0); close(fd);
    game_bytes_read = 0; changed = false;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed && game_bytes_read > 0);
    CHECK(access(flag, F_OK) != 0);
    size_t asset_offset = get_u64(bundle + 48 + 240 + 192);
    u8 actual[sizeof("a bundled game asset whose same-size damage needs full verification")];
    read_exact("drive_c/WA2/data.bin", actual, sizeof(actual) - 1);
    CHECK(memcmp(actual, bundle + asset_offset, sizeof(actual) - 1) == 0);
    char asset_path[PATH_MAX]; path_for(asset_path, "drive_c/WA2/data.bin");
    CHECK(truncate(asset_path, 1) == 0);
    changed = false;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    CHECK(truncate(asset_path, 1) == 0);
    char part_path[PATH_MAX]; path_for(part_path, "drive_c/WA2/data.bin.part");
    int part_fd = open(part_path, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(part_fd >= 0);
    CHECK(write(part_fd, bundle + asset_offset, sizeof(actual) - 1) == (ssize_t)(sizeof(actual) - 1));
    close(part_fd);
    available_bytes = 1024 * 1024;
    int before_writes = payload_writes;
    changed = false;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    CHECK(payload_writes == before_writes && !exists("drive_c/WA2/data.bin.part"));
    available_bytes = INT64_MAX / 2;
    int flag_fd = open(flag, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(flag_fd >= 0); close(flag_fd);
    char nro_path[PATH_MAX]; path_for(nro_path, "wine-nx-runtime.nro");
    CHECK(truncate(nro_path, 1) == 0);
    available_bytes = 0; changed = false;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == MAKERESULT(Module_HomebrewLoader, 101));
    CHECK(access(flag, F_OK) == 0);
    available_bytes = INT64_MAX / 2;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    CHECK(access(flag, F_OK) != 0);
    free(bundle);
    printf("PASS game cache, forced hash, and size repair\n");
}
static void large_bounds_cases(const u8 *basic) {
    u8 table[48 + 2 * 240];
    memcpy(table, basic, sizeof(table));
    u64 first_offset = sizeof(table), first_size = 0xffffffffull;
    u64 second_offset = first_offset + first_size, second_size = 1;
    put_u64(table + 48 + 192, first_offset); put_u64(table + 48 + 200, first_size);
    put_u64(table + 288 + 192, second_offset); put_u64(table + 288 + 200, second_size);
    FsStorage storage = { table, second_offset + second_size };
    bool changed = false;
    available_bytes = 0;
    CHECK(runtime_deploy(&storage, 0, storage.length, &changed) == MAKERESULT(Module_HomebrewLoader, 101));
    CHECK(second_offset > 0xffffffffull && !changed);
    put_u64(table + 48 + 200, first_size + 1);
    CHECK(runtime_deploy(&storage, 0, storage.length, &changed) == MAKERESULT(Module_HomebrewLoader, 100));
    available_bytes = INT64_MAX / 2;
    CHECK(runtime_deploy(&storage, 0, 32ull * 1024 * 1024 * 1024 + 1, &changed) == MAKERESULT(Module_HomebrewLoader, 100));
    const u32 count = 16384;
    size_t table_length = 48 + (size_t)count * 240;
    u8 *many = calloc(1, table_length); CHECK(many);
    memcpy(many, basic, 48); memcpy(many + 12, &count, 4);
    for (u32 i = 0; i < count; i++) {
        u8 *entry = many + 48 + (size_t)i * 240;
        if (i == 0) strcpy((char *)entry, "wine-nx-runtime.nro");
        else if (i == count - 1) strcpy((char *)entry, "drive_c/WA2/WA2_full_menu.exe");
        else snprintf((char *)entry, 192, "a%05u", i);
        u64 offset = table_length; put_u64(entry + 192, offset);
    }
    FsStorage maximum = { many, table_length };
    available_bytes = 0;
    CHECK(runtime_deploy(&maximum, 0, maximum.length, &changed) == MAKERESULT(Module_HomebrewLoader, 101));
    available_bytes = INT64_MAX / 2;
    free(many);
    u8 collision[48 + 5 * 240] = {0};
    memcpy(collision, basic, 48);
    u32 five = 5; memcpy(collision + 12, &five, 4);
    const char *paths[] = { "wine-nx-runtime.nro", "a", "a-file", "a/b",
                            "drive_c/WA2/WA2_full_menu.exe" };
    for (int i = 0; i < 5; i++) {
        u8 *entry = collision + 48 + i * 240;
        strcpy((char *)entry, paths[i]);
        u64 at = sizeof(collision); put_u64(entry + 192, at);
    }
    FsStorage conflict = { collision, sizeof(collision) };
    CHECK(runtime_deploy(&conflict, 0, conflict.length, &changed) == MAKERESULT(Module_HomebrewLoader, 100));
    strcpy((char *)(collision + 48 + 240), "drive_c/users/save.dat");
    CHECK(runtime_deploy(&conflict, 0, conflict.length, &changed) == MAKERESULT(Module_HomebrewLoader, 100));
    printf("PASS >4 GiB offsets, FAT32 file limit, and 32 GiB bundle limit\n");
}

int main(void) {
    char temp[] = "/tmp/autorun-nsp-deploy-XXXXXX";
    CHECK(mkdtemp(temp)); strcpy(sd_root, temp);
    size_t length; u8 *bundle = load_bundle(&length);
    FsStorage storage = { bundle, length }; bool changed = false;
    const size_t first_offset = (size_t)get_u64(bundle + 48 + 192);
    const size_t first_size = (size_t)get_u64(bundle + 48 + 200);
    const size_t second_offset = (size_t)get_u64(bundle + 288 + 192);
    const size_t second_size = (size_t)get_u64(bundle + 288 + 200);
    CHECK(first_offset + first_size == second_offset);
    CHECK(second_offset + second_size == length);
    printf("bundle %zu bytes, probe %zu, message %zu\n", length, first_size, second_size);

    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    CHECK(exists("ready.sha256") && exists("wine-nx-runtime.nro") && exists("drive_c/WA2/WA2_full_menu.exe"));
    CHECK(exists("config/settings.json") && exists("drive_c/WA2/WA2_full_menu.wine-nx.txt"));
    u8 *actual = malloc(first_size > second_size ? first_size : second_size); CHECK(actual);
    read_exact("wine-nx-runtime.nro", actual, first_size);
    CHECK(memcmp(actual, bundle + first_offset, first_size) == 0);
    read_exact("drive_c/WA2/WA2_full_menu.exe", actual, second_size);
    CHECK(memcmp(actual, bundle + second_offset, second_size) == 0);
    int writes = total_writes, prior_renames = renames;
    changed = true;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && !changed);
    CHECK(writes == total_writes && prior_renames == renames);
    printf("PASS first deploy and cached integrity check\n");
    seed_cases(&storage, length);

    mutate_cases(bundle, length);
    printf("PASS malformed path and offset rejection\n");

    u8 wrong = 0; write_exact("drive_c/WA2/WA2_full_menu.exe", &wrong, 1);
    changed = false; prior_renames = renames;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    CHECK(renames == prior_renames + 1);
    read_exact("drive_c/WA2/WA2_full_menu.exe", actual, second_size);
    CHECK(memcmp(actual, bundle + second_offset, second_size) == 0);
    printf("PASS corrupt resource repaired alone\n");

    write_exact("wine-nx-runtime.nro", &wrong, 1);
    available_bytes = 0; changed = false;
    CHECK(runtime_deploy(&storage, 0, length, &changed) != 0 && !changed);
    CHECK(!exists("ready.sha256"));
    available_bytes = INT64_MAX / 2;
    payload_writes = 0; fail_write_number = 1;
    CHECK(runtime_deploy(&storage, 0, length, &changed) != 0 && !changed);
    CHECK(!exists("ready.sha256") && exists("wine-nx-runtime.nro.part"));
    fail_write_number = 0;
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    CHECK(exists("ready.sha256") && !exists("wine-nx-runtime.nro.part"));
    read_exact("wine-nx-runtime.nro", actual, first_size);
    CHECK(memcmp(actual, bundle + first_offset, first_size) == 0);
    printf("PASS low space, interrupted copy, and retry\n");

    write_exact("drive_c/WA2/WA2_full_menu.exe", &wrong, 1);
    CHECK(exists("ready.sha256"));
    CHECK(runtime_deploy(&storage, 0, length, &changed) == 0 && changed);
    printf("PASS marker alone is not trusted\n");
    game_cache_cases(bundle);
    large_bounds_cases(bundle);
    free(actual); free(bundle);
    printf("PASS %d assertions\n", checks);
    return 0;
}
