/*
 * Builds the three NCAs a forwarder is made of, on this machine, and takes them
 * apart again: the sections the console will read, the romfs the loader reads
 * the NRO path out of, and the NPDM field that decides the address space.
 *
 * Nothing is installed. The content store is a directory and NCA headers are
 * encrypted with the user-supplied header key.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "switch.h"

/* OpenSSL ABI declarations: the build container has libcrypto but no headers. */
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
typedef struct evp_cipher_st EVP_CIPHER;
extern unsigned char *SHA256(const unsigned char *, size_t, unsigned char *);
extern const EVP_CIPHER *EVP_aes_128_xts(void);
extern EVP_CIPHER_CTX *EVP_CIPHER_CTX_new(void);
extern void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *);
extern int EVP_EncryptInit_ex(EVP_CIPHER_CTX *, const EVP_CIPHER *, void *, const unsigned char *, const unsigned char *);
extern int EVP_EncryptUpdate(EVP_CIPHER_CTX *, unsigned char *, int *, const unsigned char *, int);
extern int EVP_EncryptFinal_ex(EVP_CIPHER_CTX *, unsigned char *, int *);

char shim_out_dir[512];
static u8 user_header_key[0x20];
static int key_half;
static u64 target_atmosphere_version;

void sha256CalculateHash( void *dst, const void *src, size_t size )
{ if (!SHA256(src, size, dst)) abort(); }

void aes128XtsContextCreate( Aes128XtsContext *ctx, const void *key0, const void *key1, int encrypt )
{ (void)encrypt; memcpy(ctx->key, key0, 16); memcpy(ctx->key + 16, key1, 16); }
void aes128XtsContextResetSector( Aes128XtsContext *ctx, u64 sector, int nintendo )
{ (void)nintendo; ctx->sector = sector; }
size_t aes128XtsEncrypt( Aes128XtsContext *ctx, void *dst, const void *src, size_t size )
{
    EVP_CIPHER_CTX *evp = EVP_CIPHER_CTX_new();
    u8 tweak[16] = {0};
    int written = 0, tail = 0, i;
    if (!evp || size > 0x7fffffff) abort();
    /* libnx's Nintendo XTS sector numbering stores the sector big-endian. */
    for (i = 0; i < 8; i++) tweak[15 - i] = (u8)(ctx->sector >> (i * 8));
    if (!EVP_EncryptInit_ex(evp, EVP_aes_128_xts(), NULL, ctx->key, tweak) ||
        !EVP_EncryptUpdate(evp, dst, &written, src, (int)size) ||
        !EVP_EncryptFinal_ex(evp, (u8 *)dst + written, &tail) ||
        written + tail != (int)size) abort();
    EVP_CIPHER_CTX_free(evp);
    return size;
}

Result ncmInitialize( void ) { return 0; }
void ncmExit( void ) {}
Result nsInitialize( void ) { return 0; }
void nsExit( void ) {}
/* Host-only adapter: explicit target firmware profile, never an assumed console version. */
Result splInitialize( void ) { return 0; }
void splExit( void ) {}
Result splGetConfig( SplConfigItem item, u64 *out )
{
    if (item != 65000) return 1;
    *out = target_atmosphere_version;
    return 0;
}
Result splCryptoInitialize( void ) { return 0; }
void splCryptoExit( void ) {}
Result splCryptoGenerateAesKek( const void *src, u32 generation, u32 option, void *out )
{ (void)src; (void)generation; (void)option; memset(out, 0, 0x10); key_half = 0; return 0; }
Result splCryptoGenerateAesKey( const void *kek, const void *src, void *out )
{ (void)kek; (void)src; if (key_half >= 2) return 1; memcpy(out, user_header_key + 16 * key_half++, 16); return 0; }

static int placeholders;
static int registered;
static char placeholder_path[512];

Result ncmOpenContentStorage( NcmContentStorage *out, NcmStorageId id ) { (void)id; out->handle = 1; return 0; }
void ncmContentStorageClose( NcmContentStorage *cs ) { (void)cs; }
Result ncmContentStorageGeneratePlaceHolderId( NcmContentStorage *cs, NcmPlaceHolderId *out )
{ (void)cs; memset( out, 0, sizeof(*out) ); out->c[0] = ++placeholders; return 0; }
Result ncmContentStorageDeletePlaceHolder( NcmContentStorage *cs, NcmPlaceHolderId *id ) { (void)cs; (void)id; return 0; }
Result ncmContentStorageCreatePlaceHolder( NcmContentStorage *cs, const NcmContentId *content,
                                           const NcmPlaceHolderId *id, u64 size )
{
    (void)cs; (void)content; (void)size;
    snprintf( placeholder_path, sizeof(placeholder_path), "%s/nca-%d.bin", shim_out_dir, id->c[0] );
    return 0;
}
Result ncmContentStorageWritePlaceHolder( NcmContentStorage *cs, const NcmPlaceHolderId *id, u64 offset,
                                          const void *data, size_t size )
{
    FILE *file;

    (void)cs; (void)id; (void)offset;
    if (!(file = fopen( placeholder_path, "wb" ))) return 1;
    fwrite( data, 1, size, file );
    fclose( file );
    return 0;
}
Result ncmContentStorageDelete( NcmContentStorage *cs, const NcmContentId *id ) { (void)cs; (void)id; return 0; }
Result ncmContentStorageRegister( NcmContentStorage *cs, const NcmContentId *content, const NcmPlaceHolderId *id )
{ (void)cs; (void)content; (void)id; registered++; return 0; }
Result ncmContentStorageHas( NcmContentStorage *cs, bool *out, const NcmContentId *id )
{ (void)cs; (void)id; *out = true; return 0; }
/* Nothing else is installed here, so there is nothing to hold ours against. */
Result ncmContentStorageGetSizeFromContentId( NcmContentStorage *cs, s64 *out, const NcmContentId *id )
{ (void)cs; (void)id; *out = 0; return 1; }
Result ncmContentStorageReadContentIdFile( NcmContentStorage *cs, void *out, size_t size,
                                           const NcmContentId *id, s64 offset )
{ (void)cs; (void)out; (void)size; (void)id; (void)offset; return 1; }
Result ncmContentMetaDatabaseList( NcmContentMetaDatabase *db, s32 *total, s32 *written, NcmContentMetaKey *keys,
                                   s32 count, NcmContentMetaType type, u64 id, u64 id_min, u64 id_max,
                                   NcmContentInstallType install_type )
{
    (void)db; (void)keys; (void)count; (void)type; (void)id; (void)id_min; (void)id_max; (void)install_type;
    *total = *written = 0;
    return 0;
}
Result ncmContentMetaDatabaseListContentInfo( NcmContentMetaDatabase *db, s32 *written, NcmContentInfo *infos,
                                              s32 count, const NcmContentMetaKey *key, s32 start )
{ (void)db; (void)infos; (void)count; (void)key; (void)start; *written = 0; return 0; }
Result ncmOpenContentMetaDatabase( NcmContentMetaDatabase *out, NcmStorageId id ) { (void)id; out->handle = 1; return 0; }
void ncmContentMetaDatabaseClose( NcmContentMetaDatabase *db ) { (void)db; }
Result ncmContentMetaDatabaseSet( NcmContentMetaDatabase *db, const NcmContentMetaKey *key,
                                  const void *data, size_t size )
{ (void)db; (void)key; (void)data; (void)size; return 0; }
Result ncmContentMetaDatabaseCommit( NcmContentMetaDatabase *db ) { (void)db; return 0; }
static Service manager_service;
Result nsGetApplicationManagerInterface( Service *out ) { *out = manager_service; return 0; }
Service *nsGetServiceSession_ApplicationManagerInterface( void ) { return &manager_service; }
/* What was taken away, and whether anything had been written by then: the
 * contents of an entry must go before the new ones are registered, or they are
 * the same ones and they go with them. */
static u64 deleted_completely[4];
static int deleted_completely_count;
static u64 deleted_entity;
static int deleted_after_write;

Result nsDeleteApplicationCompletely( u64 id )
{
    if (placeholders) deleted_after_write = 1;
    if (deleted_completely_count < 4) deleted_completely[deleted_completely_count++] = id;
    return 0;
}
Result nsDeleteApplicationEntity( u64 id )
{
    if (placeholders) deleted_after_write = 1;
    deleted_entity = id;
    return 0;
}
void serviceClose( Service *s ) { (void)s; }
int hosversionAtLeast( int major, int minor, int micro ) { (void)major; (void)minor; (void)micro; return 1; }

/* The loader and the icons the real build embeds; here, something the right
 * shape, with an NPDM taken from the one devkitPro's npdmtool wrote. */
const unsigned char *wine_nx_hbl_main;
size_t wine_nx_hbl_main_size;
const unsigned char *wine_nx_hbl_npdm;
size_t wine_nx_hbl_npdm_size;
const unsigned char *wine_nx_icon_32bit;
size_t wine_nx_icon_32bit_size;
const unsigned char *wine_nx_icon_any;
size_t wine_nx_icon_any_size;
const unsigned char *host_bundle;
size_t host_bundle_size;

#define WINE_NX_FORWARDER_EMBED_EXTERN
#include "forwarder.c"
#include "stream_packager.c"

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long length;
    if (!file || fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) || !(data = malloc((size_t)length + 1)) ||
        fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "cannot read input file: %s\n", path);
        exit(2);
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void read_header_key(const char *path)
{
    size_t size, i;
    unsigned char *file = read_file(path, &size);
    const char *hex = (const char *)file;
    int high, low;
    file[size] = 0;
    if (size == 32) memcpy(user_header_key, file, 32);
    else {
        const char *field = strstr(hex, "header_key");
        if (field) {
            hex = strchr(field, '=');
            if (!hex) { fprintf(stderr, "header_key field malformed\n"); exit(2); }
            hex++;
        }
        while (*hex == ' ' || *hex == '\t') hex++;
        if (strlen(hex) < 64) { fprintf(stderr, "header_key must be 32 bytes\n"); exit(2); }
        for (i = 0; i < 32; i++) {
            high = hex_digit(hex[i * 2]); low = hex_digit(hex[i * 2 + 1]);
            if (high < 0 || low < 0) { fprintf(stderr, "invalid header_key hex\n"); exit(2); }
            user_header_key[i] = (u8)((high << 4) | low);
        }
    }
    memset(file, 0, size);
    free(file);
}

int main(int argc, char **argv)
{
    struct wine_nx_forwarder request;
    const char *step = NULL;
    unsigned result;
    if (argc != 11 + PROGRAM_ASSET_COUNT) {
        fprintf(stderr, "usage: packager main.nso main.npdm icon.jpg bundle.bin keyfile nca-dir nro-path name author atmosphere-version %d asset files\n", PROGRAM_ASSET_COUNT);
        return 2;
    }
    unsigned major, minor, micro; char extra;
    if (sscanf(argv[10], "%u.%u.%u%c", &major, &minor, &micro, &extra) != 3 ||
        major > 255 || minor > 255 || micro > 255) {
        fprintf(stderr, "invalid target Atmosphere version\n"); return 2;
    }
    target_atmosphere_version = ((u64)major << 56) | ((u64)minor << 48) | ((u64)micro << 40);
    read_header_key(argv[5]);
    snprintf(shim_out_dir, sizeof(shim_out_dir), "%s", argv[6]);
    wine_nx_hbl_main = read_file(argv[1], &wine_nx_hbl_main_size);
    wine_nx_hbl_npdm = read_file(argv[2], &wine_nx_hbl_npdm_size);
    wine_nx_icon_any = read_file(argv[3], &wine_nx_icon_any_size);
    request = (struct wine_nx_forwarder){
        .nro_path = argv[7], .args = "sdmc:/switch/autorun-games/wa2-full/drive_c/WA2/WA2_full_menu.exe", .name = argv[8], .author = argv[9],
        .address_space = WINE_NX_SPACE_32BIT_NO_ALIAS,
        .icon = wine_nx_icon_any, .icon_size = wine_nx_icon_any_size
    };
    result = host_stream_packager(&request, argv[4], (const char *const *)(argv + 11), &step);
    memset(user_header_key, 0, sizeof(user_header_key));
    if (result) {
        fprintf(stderr, "NCA build failed at %s (code %u)\n", step ? step : "unknown step", result);
        return 1;
    }
    printf("built three NCAs for title 0500A17E00070000\n");
    return 0;
}
