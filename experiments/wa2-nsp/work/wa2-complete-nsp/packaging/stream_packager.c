/* Host-only, bounded-memory NCA writer. Included after forwarder.c to reuse its
 * on-disk structures and the established small control/meta builders. */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>

typedef struct evp_md_ctx_st EVP_MD_CTX;
typedef struct evp_md_st EVP_MD;
extern EVP_MD_CTX *EVP_MD_CTX_new(void);
extern void EVP_MD_CTX_free(EVP_MD_CTX *);
extern const EVP_MD *EVP_sha256(void);
extern int EVP_DigestInit_ex(EVP_MD_CTX *, const EVP_MD *, void *);
extern int EVP_DigestUpdate(EVP_MD_CTX *, const void *, size_t);
extern int EVP_DigestFinal_ex(EVP_MD_CTX *, unsigned char *, unsigned int *);

#define STREAM_CHUNK 0x10000
#include "asset_names.h"
#define PROGRAM_ROMFS_FILES (3 + PROGRAM_ASSET_COUNT)
#define PROGRAM_ROMFS_HASH_BUCKETS 127
static u8 stream_chunk[STREAM_CHUNK];

static int stream_seek(FILE *f, u64 pos) { return pos <= INT64_MAX && !fseeko(f, (off_t)pos, SEEK_SET); }
static u64 stream_size(FILE *f)
{
    off_t pos;
    if (fseeko(f, 0, SEEK_END) || (pos = ftello(f)) < 0) return UINT64_MAX;
    return (u64)pos;
}
static int stream_write(FILE *f, const void *p, size_t n) { return fwrite(p, 1, n, f) == n; }
static int stream_zero(FILE *f, u64 n)
{
    static const u8 zero[STREAM_CHUNK];
    while (n) { size_t k = n < sizeof(zero) ? (size_t)n : sizeof(zero); if (!stream_write(f, zero, k)) return 0; n -= k; }
    return 1;
}
static int stream_copy(FILE *dst, FILE *src, u64 n)
{
    while (n) { size_t k = n < STREAM_CHUNK ? (size_t)n : STREAM_CHUNK;
        if (fread(stream_chunk, 1, k, src) != k || !stream_write(dst, stream_chunk, k)) return 0;
        n -= k;
    }
    return 1;
}
static int stream_digest(FILE *src, u64 n, u8 digest[32])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new(); unsigned int length = 0; int ok = ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    while (ok && n) { size_t k = n < STREAM_CHUNK ? (size_t)n : STREAM_CHUNK;
        ok = fread(stream_chunk, 1, k, src) == k && EVP_DigestUpdate(ctx, stream_chunk, k);
        n -= k;
    }
    if (ok) ok = EVP_DigestFinal_ex(ctx, digest, &length) && length == 32;
    EVP_MD_CTX_free(ctx); return ok;
}
static int digest_file(const char *path, u8 digest[32], u64 *length)
{
    FILE *f = fopen(path, "rb"); int ok;
    if (!f) return 0;
    *length = stream_size(f);
    ok = *length != UINT64_MAX && stream_seek(f, 0) && stream_digest(f, *length, digest);
    fclose(f); return ok;
}
static int write_buf_file(const char *path, const struct buf *b)
{
    FILE *f = fopen(path, "wb"); int ok = f && stream_write(f, b->data, b->size);
    if (f && fclose(f)) ok = 0;
    return ok;
}
static int hash_file_layer(FILE *src, u64 source_size, u32 block, FILE *dst, u64 *out_size)
{
    u64 left = source_size; u8 hash[32];
    if (!stream_seek(src, 0) || !stream_seek(dst, 0)) return 0;
    while (left) { size_t k = left < block ? (size_t)left : block;
        if (fread(stream_chunk, 1, k, src) != k) return 0;
        sha256CalculateHash(hash, stream_chunk, k);
        if (!stream_write(dst, hash, 32)) return 0;
        left -= k;
    }
    *out_size = (source_size + block - 1) / block * 32;
    return 1;
}

/* Flat Program RomFS: bundle and WA2 PAK are streamed independently. */
static int stream_romfs(FILE *out, const struct file_entry entries[PROGRAM_ROMFS_FILES],
                        const char *bundle_path, const char *const *asset_paths,
                        u64 bundle_size, const u64 asset_sizes[PROGRAM_ASSET_COUNT], u64 *unpadded, u64 *padded)
{
    u32 dir_hash[PROGRAM_ROMFS_HASH_BUCKETS], file_hash[PROGRAM_ROMFS_HASH_BUCKETS];
    u32 offsets[PROGRAM_ROMFS_FILES];
    u8 dir_table[0x18], file_table[PROGRAM_ROMFS_FILES * (sizeof(romfs_file) + 192)];
    u64 data_offsets[PROGRAM_ROMFS_FILES], partition = 0;
    u32 at = 0; int i; romfs_header header; romfs_dir *root;
    FILE *bundle = NULL, *asset = NULL;
    memset(dir_table, 0, sizeof(dir_table)); memset(file_table, 0, sizeof(file_table));
    for (i = 0; i < PROGRAM_ROMFS_FILES; i++) { size_t len = strlen(entries[i].name) - 1;
        if (at + sizeof(romfs_file) + align32((u32)len, 4) > sizeof(file_table)) return 0;
        partition = align64(partition, 0x10); data_offsets[i] = partition;
        if (UINT64_MAX - partition < entries[i].size) return 0;
        partition += entries[i].size; offsets[i] = at;
        at += sizeof(romfs_file) + align32((u32)len, 4);
    }
    for (i = 0; i < PROGRAM_ROMFS_HASH_BUCKETS; i++) dir_hash[i] = file_hash[i] = ROMFS_ENTRY_EMPTY;
    for (i = 0; i < PROGRAM_ROMFS_FILES; i++) { u32 len = (u32)strlen(entries[i].name) - 1;
        u32 hash = romfs_path_hash(0, (const u8 *)entries[i].name, 1, len);
        romfs_file entry = {0}; entry.parent = 0;
        entry.sibling = i + 1 < PROGRAM_ROMFS_FILES ? offsets[i + 1] : ROMFS_ENTRY_EMPTY;
        entry.dataOff = data_offsets[i]; entry.dataSize = entries[i].size;
        entry.nextHash = file_hash[hash % PROGRAM_ROMFS_HASH_BUCKETS];
        file_hash[hash % PROGRAM_ROMFS_HASH_BUCKETS] = offsets[i]; entry.nameLen = len;
        memcpy(file_table + offsets[i], &entry, sizeof(entry));
        memcpy(file_table + offsets[i] + sizeof(entry), entries[i].name + 1, len);
    }
    root = (romfs_dir *)dir_table; root->parent = 0; root->sibling = ROMFS_ENTRY_EMPTY;
    root->childDir = ROMFS_ENTRY_EMPTY; root->childFile = offsets[0];
    u32 root_bucket = romfs_path_hash(0, NULL, 0, 0) % PROGRAM_ROMFS_HASH_BUCKETS;
    root->nextHash = dir_hash[root_bucket]; dir_hash[root_bucket] = 0; root->nameLen = 0;
    /* Verify every entry is reachable through the same hash buckets used by libnx. */
    if (dir_hash[root_bucket] != 0) return 0;
    for (i = 0; i < PROGRAM_ROMFS_FILES; i++) {
        u32 len = (u32)strlen(entries[i].name) - 1;
        u32 bucket = romfs_path_hash(0, (const u8 *)entries[i].name, 1, len) % PROGRAM_ROMFS_HASH_BUCKETS;
        u32 cursor = file_hash[bucket];
        int found = 0;
        for (int steps = 0; steps < PROGRAM_ROMFS_FILES && cursor != ROMFS_ENTRY_EMPTY; steps++) {
            romfs_file candidate;
            if (cursor > at || at - cursor < sizeof(candidate)) return 0;
            memcpy(&candidate, file_table + cursor, sizeof(candidate));
            if (candidate.nameLen > at - cursor - sizeof(candidate)) return 0;
            if (candidate.nameLen == len &&
                !memcmp(file_table + cursor + sizeof(candidate), entries[i].name + 1, len)) {
                found = 1; break;
            }
            cursor = candidate.nextHash;
        }
        if (!found) return 0;
    }
    memset(&header, 0, sizeof(header)); header.headerSize = sizeof(header);
    header.fileHashTableSize = sizeof(file_hash); header.fileTableSize = at;
    header.dirHashTableSize = sizeof(dir_hash); header.dirTableSize = sizeof(dir_table);
    header.fileDataOff = ROMFS_FILEPARTITION_OFS;
    header.dirHashTableOff = align64(partition + ROMFS_FILEPARTITION_OFS, 4);
    header.dirTableOff = header.dirHashTableOff + header.dirHashTableSize;
    header.fileHashTableOff = header.dirTableOff + header.dirTableSize;
    header.fileTableOff = header.fileHashTableOff + header.fileHashTableSize;
    if (!stream_seek(out, 0) || !stream_write(out, &header, sizeof(header))) return 0;
    for (i = 0; i < PROGRAM_ROMFS_FILES; i++) {
        if (!stream_seek(out, data_offsets[i] + ROMFS_FILEPARTITION_OFS)) return 0;
        if (i == 2) { bundle = fopen(bundle_path, "rb");
            if (!bundle || !stream_copy(out, bundle, bundle_size)) { if (bundle) fclose(bundle); return 0; }
            fclose(bundle);
        } else if (i >= 3) {
            asset = fopen(asset_paths[i-3], "rb");
            if (!asset || stream_size(asset) != asset_sizes[i-3] || !stream_seek(asset, 0) ||
                !stream_copy(out, asset, asset_sizes[i-3])) {
                if (asset) fclose(asset);
                return 0;
            }
            fclose(asset);
        } else if (!stream_write(out, entries[i].data, entries[i].size)) return 0;
    }
    if (!stream_seek(out, header.dirHashTableOff) ||
        !stream_write(out, dir_hash, sizeof(dir_hash)) || !stream_write(out, dir_table, sizeof(dir_table)) ||
        !stream_write(out, file_hash, sizeof(file_hash)) || !stream_write(out, file_table, at)) return 0;
    *unpadded = header.fileTableOff + at;
    *padded = *unpadded + IVFC_HASH_BLOCK_SIZE - (*unpadded % IVFC_HASH_BLOCK_SIZE);
    return stream_zero(out, *padded - *unpadded) && !fflush(out);
}

static int stream_program(const char *path, const char *bundle_path, const char *const *asset_paths,
                          const struct wine_nx_forwarder *request,
                          const u8 *header_key, u64 tid, u64 *output_size)
{
    struct nca_header header = {0}; struct buf exefs = {0};
    struct file_entry files[PROGRAM_ROMFS_FILES], execs[2];
    FILE *levels[6] = {0}, *out = NULL; u64 sizes[6] = {0}, romfs_real = 0, start, end;
    struct nca_fs_header *fs = &header.fs_header[1];
    struct integrity_meta_info *meta = &fs->hash_data.integrity_meta_info;
    char args[1024]; u8 *npdm = NULL; int i, ok = 0;
    struct stat st, asset_st; u64 asset_sizes[PROGRAM_ASSET_COUNT];
    if (stat(bundle_path, &st) || st.st_size < 0 || (u64)st.st_size > 32ull * 1024 * 1024 * 1024) goto done;
    for(int j=0;j<PROGRAM_ASSET_COUNT;j++) {
        if(stat(asset_paths[j],&asset_st) || !S_ISREG(asset_st.st_mode) || asset_st.st_size<=0 || (u64)asset_st.st_size>0xffffffffull) goto done;
        asset_sizes[j]=asset_st.st_size;
    }
    npdm = malloc(wine_nx_hbl_npdm_size);
    if (!npdm) goto done;
    memcpy(npdm, wine_nx_hbl_npdm, wine_nx_hbl_npdm_size);
    if (!npdm_patch(npdm, wine_nx_hbl_npdm_size, tid, request->address_space)) goto done;
    if (request->args && request->args[0]) snprintf(args, sizeof(args), "%s %s", request->nro_path, request->args);
    else snprintf(args, sizeof(args), "%s", request->nro_path);
    execs[0] = (struct file_entry){"main", wine_nx_hbl_main, wine_nx_hbl_main_size};
    execs[1] = (struct file_entry){"main.npdm", npdm, wine_nx_hbl_npdm_size};
    files[0] = (struct file_entry){"/nextArgv", args, strlen(args)};
    files[1] = (struct file_entry){"/nextNroPath", request->nro_path, strlen(request->nro_path)};
    files[2] = (struct file_entry){"/bundle.bin", NULL, (size_t)st.st_size};
    for (i = 0; i < PROGRAM_ASSET_COUNT; i++)
        files[3 + i] = (struct file_entry){program_asset_names[i], NULL, (size_t)asset_sizes[i]};
    if (!buf_zero(&exefs, sizeof(header)) || !nca_write_pfs0(&header, 0, execs, 2, PFS0_EXEFS_HASH_BLOCK, &exefs)) goto done;
    for (i = 0; i < 6; i++) if (!(levels[i] = tmpfile())) goto done;
    if (!stream_romfs(levels[5], files, bundle_path, asset_paths, st.st_size,
                      asset_sizes, &romfs_real, &sizes[5])) goto done;
    meta->info_level_hash.levels[5].hash_data_size = romfs_real;
    for (i = 4; i >= 0; i--) {
        u64 raw;
        if (!hash_file_layer(levels[i + 1], sizes[i + 1], IVFC_HASH_BLOCK_SIZE, levels[i], &raw)) goto done;
        sizes[i] = raw + IVFC_HASH_BLOCK_SIZE - (raw % IVFC_HASH_BLOCK_SIZE);
        if (!stream_zero(levels[i], sizes[i] - raw) || fflush(levels[i])) goto done;
        meta->info_level_hash.levels[i].hash_data_size = sizes[i];
        meta->info_level_hash.levels[i].block_size = 0x0e;
    }
    for (i = 1; i < 6; i++) meta->info_level_hash.levels[i].logical_offset =
        meta->info_level_hash.levels[i - 1].logical_offset + meta->info_level_hash.levels[i - 1].hash_data_size;
    if (!stream_seek(levels[0], 0) || !stream_digest(levels[0], sizes[0], meta->master_hash)) goto done;
    out = fopen(path, "wb+"); if (!out || !stream_write(out, exefs.data, exefs.size)) goto done;
    start = exefs.size;
    for (i = 0; i < 6; i++) {
        if (!stream_seek(levels[i], 0) || !stream_copy(out, levels[i], sizes[i])) goto done;
    }
    end = start;
    for (i = 0; i < 6; i++) end += sizes[i];
    if (!stream_zero(out, 0x200 - end % 0x200)) goto done;
    end += 0x200 - end % 0x200;
    nca_write_section(&header, 1, start, end);
    fs->hash_type = NCA_HASH_INTEGRITY; fs->fs_type = NCA_FS_ROMFS;
    fs->version = 2; fs->encryption_type = NCA_ENCRYPTION_NONE;
    meta->magic = IVFC_MAGIC; meta->version = 0x20000; meta->master_hash_size = 32;
    meta->info_level_hash.max_layers = 7; meta->info_level_hash.levels[5].block_size = 0x0e;
    sha256CalculateHash(header.fs_header_hash[1], fs, sizeof(*fs));
    header.magic = NCA3_MAGIC; header.content_type = NCA_CONTENT_PROGRAM;
    header.program_id = tid; header.sdk_version = 0x000C1100; header.size = end;
    { Aes128XtsContext ctx; u64 pos; u8 sector = 0;
      aes128XtsContextCreate(&ctx, header_key, header_key + 16, true);
      for (pos = 0; pos < 0xC00; pos += 0x200) {
          aes128XtsContextResetSector(&ctx, sector++, true);
          aes128XtsEncrypt(&ctx, (u8 *)&header + pos, (u8 *)&header + pos, 0x200);
      }
    }
    if (!stream_seek(out, 0) || !stream_write(out, &header, sizeof(header)) || fflush(out)) goto done;
    *output_size = end; ok = 1;
done:
    if (out && fclose(out)) ok = 0;
    for (i = 0; i < 6; i++) if (levels[i]) fclose(levels[i]);
    buf_free(&exefs); free(npdm); return ok;
}

static unsigned host_stream_packager(const struct wine_nx_forwarder *request,
                                     const char *bundle_path, const char *const *asset_paths,
                                     const char **step)
{
    const u64 tid = 0x0500A17E00070000ull;
    struct nca_header header = {0}; struct buf control = {0}, meta = {0}, cnmt = {0};
    struct file_entry romfs[2], cnmt_entry; NacpStruct *nacp = NULL;
    struct cnmt_header cnmt_header = {0}; NcmApplicationMetaExtendedHeader extended = {0};
    NcmPackagedContentInfo contents[2] = {{0}};
    u8 hashes[3][32], digest[32], header_key[32];
    u64 sizes[3]; char cnmt_name[40], path[512]; int i; unsigned rc = 1;
    *step = "deriving the header key";
    memcpy(header_key, user_header_key, sizeof(header_key));
    *step = "streaming the program";
    snprintf(path, sizeof(path), "%s/nca-1.bin", shim_out_dir);
    if (!stream_program(path, bundle_path, asset_paths, request, header_key, tid, &sizes[0]) ||
        !digest_file(path, hashes[0], &sizes[0])) goto done;
    *step = "building the control";
    nacp = calloc(1, sizeof(*nacp)); if (!nacp) goto done;
    if (!nacp_from_nro(request->nro_path, nacp)) nacp_defaults(nacp);
    nacp_build(nacp, request->name, request->author, tid);
    romfs[0] = (struct file_entry){"/control.nacp", nacp, sizeof(*nacp)};
    romfs[1] = (struct file_entry){"/icon_AmericanEnglish.dat", request->icon, request->icon_size};
    if (!buf_zero(&control, sizeof(header)) || !nca_write_romfs(&header, 0, romfs, 2, &control)) goto done;
    nca_finish(&header, tid, NCA_CONTENT_CONTROL, header_key, &control);
    sizes[1] = control.size; sha256CalculateHash(hashes[1], control.data, control.size);
    snprintf(path, sizeof(path), "%s/nca-2.bin", shim_out_dir);
    if (!write_buf_file(path, &control)) goto done;
    *step = "building the meta";
    cnmt_header.title_id = tid; cnmt_header.meta_type = NcmContentMetaType_Application;
    cnmt_header.meta_header.extended_header_size = sizeof(extended);
    cnmt_header.meta_header.content_count = 2; extended.patch_id = tid | 0x800;
    for (i = 0; i < 2; i++) {
        memcpy(contents[i].hash, hashes[i], 32);
        memcpy(&contents[i].info.content_id, hashes[i], sizeof(contents[i].info.content_id));
        contents[i].info.content_type = i ? NcmContentType_Control : NcmContentType_Program;
        ncmU64ToContentInfoSize(sizes[i], &contents[i].info);
    }
    snprintf(cnmt_name, sizeof(cnmt_name), "Application_%016llX.cnmt", (unsigned long long)tid);
    if (!buf_write(&cnmt, &cnmt_header, sizeof(cnmt_header)) ||
        !buf_write(&cnmt, &extended, sizeof(extended)) ||
        !buf_write(&cnmt, contents, sizeof(contents))) goto done;
    sha256CalculateHash(digest, cnmt.data, cnmt.size);
    if (!buf_write(&cnmt, digest, 32)) goto done;
    cnmt_entry = (struct file_entry){cnmt_name, cnmt.data, cnmt.size};
    memset(&header, 0, sizeof(header));
    if (!buf_zero(&meta, sizeof(header)) ||
        !nca_write_pfs0(&header, 0, &cnmt_entry, 1, PFS0_META_HASH_BLOCK, &meta)) goto done;
    nca_finish(&header, tid, NCA_CONTENT_META, header_key, &meta);
    snprintf(path, sizeof(path), "%s/nca-3.bin", shim_out_dir);
    if (!write_buf_file(path, &meta)) goto done;
    rc = 0; *step = NULL;
done:
    memset(header_key, 0, sizeof(header_key)); free(nacp);
    buf_free(&control); buf_free(&meta); buf_free(&cnmt);
    return rc;
}
