/*
 * Builds the three NCAs a forwarder is made of, on this machine, and takes them
 * apart again: the sections the console will read, the romfs the loader reads
 * the NRO path out of, and the NPDM field that decides the address space.
 *
 * Nothing is installed. The content store is a directory, and the header is
 * left in the clear so it can be read back.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "switch_shim.h"

char shim_out_dir[512];

/* The hash only has to be a hash: what is checked here is the shape of what it
 * is taken over, not the console's acceptance of it. */
void sha256CalculateHash( void *dst, const void *src, size_t size )
{
    const u8 *bytes = src;
    u64 state[4] = { 0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull };
    size_t i;

    for (i = 0; i < size; i++)
    {
        int slot = i & 3;
        state[slot] = (state[slot] ^ bytes[i]) * 0x100000001b3ull;
        state[slot] ^= state[slot] >> 29;
    }
    state[0] ^= size;
    memcpy( dst, state, 0x20 );
}

/* Left in the clear, so the test can read the header back. */
void aes128XtsContextCreate( Aes128XtsContext *ctx, const void *key0, const void *key1, int encrypt )
{ (void)key0; (void)key1; (void)encrypt; memset( ctx, 0, sizeof(*ctx) ); }
void aes128XtsContextResetSector( Aes128XtsContext *ctx, u64 sector, int nintendo )
{ (void)nintendo; ctx->sector = sector; }
size_t aes128XtsEncrypt( Aes128XtsContext *ctx, void *dst, const void *src, size_t size )
{ (void)ctx; if (dst != src) memcpy( dst, src, size ); return size; }

Result ncmInitialize( void ) { return 0; }
void ncmExit( void ) {}
Result nsInitialize( void ) { return 0; }
void nsExit( void ) {}
Result splCryptoInitialize( void ) { return 0; }
void splCryptoExit( void ) {}
Result splCryptoGenerateAesKek( const void *src, u32 generation, u32 option, void *out )
{ (void)generation; (void)option; memcpy( out, src, 0x10 ); memset( (u8 *)out + 0x10, 0, 0x10 ); return 0; }
Result splCryptoGenerateAesKey( const void *kek, const void *src, void *out )
{ (void)kek; memcpy( out, src, 0x10 ); return 0; }

static int placeholders;
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
{ (void)cs; (void)content; (void)id; return 0; }
Result ncmOpenContentMetaDatabase( NcmContentMetaDatabase *out, NcmStorageId id ) { (void)id; out->handle = 1; return 0; }
void ncmContentMetaDatabaseClose( NcmContentMetaDatabase *db ) { (void)db; }
Result ncmContentMetaDatabaseSet( NcmContentMetaDatabase *db, const NcmContentMetaKey *key,
                                  const void *data, size_t size )
{ (void)db; (void)key; (void)data; (void)size; return 0; }
Result ncmContentMetaDatabaseCommit( NcmContentMetaDatabase *db ) { (void)db; return 0; }
static Service manager_service;
Result nsGetApplicationManagerInterface( Service *out ) { *out = manager_service; return 0; }
Service *nsGetServiceSession_ApplicationManagerInterface( void ) { return &manager_service; }
Result nsDeleteApplicationCompletely( u64 id ) { (void)id; return 0; }
Result nsDeleteApplicationEntity( u64 id ) { (void)id; return 0; }
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

#define WINE_NX_FORWARDER_EMBED_EXTERN
#include "../source/forwarder.c"

static void *load_file( const char *path, size_t *size )
{
    FILE *file = fopen( path, "rb" );
    void *data;
    long length;

    if (!file) { fprintf( stderr, "cannot open %s\n", path ); exit( 2 ); }
    fseek( file, 0, SEEK_END );
    length = ftell( file );
    fseek( file, 0, SEEK_SET );
    data = malloc( length );
    if (fread( data, 1, length, file ) != (size_t)length) exit( 2 );
    fclose( file );
    *size = length;
    return data;
}

static u8 *read_nca( int index, size_t *size )
{
    char path[512];

    snprintf( path, sizeof(path), "%s/nca-%d.bin", shim_out_dir, index );
    return load_file( path, size );
}

/* Walk a section of the NCA the way the console would: the table says where. */
static const struct nca_header *nca_of( const u8 *data ) { return (const struct nca_header *)data; }

static void check_program( const u8 *data, size_t size, const char *nro_path )
{
    const struct nca_header *header = nca_of( data );
    u64 romfs_start = (u64)header->fs_table[1].media_start_offset * 0x200;
    const struct integrity_level *levels = header->fs_header[1].hash_data.integrity_meta_info.info_level_hash.levels;
    const romfs_header *romfs;
    const romfs_dir *root;
    const u8 *base;
    u32 offset;
    int found = 0;

    assert( header->magic == NCA3_MAGIC );
    assert( header->content_type == NCA_CONTENT_PROGRAM );
    assert( header->size == size );
    /* An exefs and a romfs, in that order, and nothing else. */
    assert( header->fs_header[0].fs_type == NCA_FS_PFS0 && header->fs_header[0].hash_type == NCA_HASH_SHA256 );
    assert( header->fs_header[1].fs_type == NCA_FS_ROMFS && header->fs_header[1].hash_type == NCA_HASH_INTEGRITY );
    assert( header->fs_header[0].encryption_type == NCA_ENCRYPTION_NONE );
    assert( !header->fs_table[2].media_start_offset && !header->fs_table[3].media_start_offset );
    assert( (u64)header->fs_table[0].media_start_offset * 0x200 == sizeof(*header) );
    assert( (u64)header->fs_table[1].media_end_offset * 0x200 <= size );

    /* The sixth level is the image; the five above it are its hashes. */
    base = data + romfs_start + levels[5].logical_offset;
    romfs = (const romfs_header *)base;
    assert( romfs->headerSize == sizeof(*romfs) );
    assert( romfs->fileDataOff == 0x200 );
    root = (const romfs_dir *)(base + romfs->dirTableOff);
    assert( root->childDir == 0xFFFFFFFFu );
    /* Four-byte aligned entries, so they are copied out before they are read. */
    for (offset = root->childFile; offset != 0xFFFFFFFFu;)
    {
        const u8 *at = base + romfs->fileTableOff + offset;
        char name[64] = {0};
        romfs_file file;

        memcpy( &file, at, sizeof(file) );
        assert( file.nameLen < sizeof(name) );
        memcpy( name, at + sizeof(file), file.nameLen );
        if (!strcmp( name, "nextNroPath" ) || !strcmp( name, "nextArgv" ))
        {
            /* With no arguments of its own, the loader is handed the path twice. */
            assert( file.dataSize == strlen( nro_path ) );
            assert( !memcmp( base + romfs->fileDataOff + file.dataOff, nro_path, file.dataSize ) );
            found |= name[4] == 'N' ? 1 : 2;
        }
        offset = file.sibling;
    }
    assert( found == 3 );
}

static void check_exefs_npdm( const u8 *data, int address_space, u64 tid )
{
    const struct nca_header *header = nca_of( data );
    u64 start = (u64)header->fs_table[0].media_start_offset * 0x200;
    const struct sha256_data *hash = &header->fs_header[0].hash_data.hierarchical_sha256_data;
    const u8 *pfs0 = data + start + hash->pfs0_layer.offset;
    const struct pfs0_header *pfs = (const struct pfs0_header *)pfs0;
    const struct pfs0_entry *entries = (const struct pfs0_entry *)(pfs0 + sizeof(*pfs));
    const char *strings = (const char *)(entries + pfs->total_files);
    const u8 *files = (const u8 *)strings + pfs->string_table_size;
    const struct npdm_meta *meta = NULL;
    const struct npdm_aci0 *aci0;
    const struct npdm_acid *acid;
    u32 i;

    assert( pfs->magic == PFS0_MAGIC );
    assert( pfs->total_files == 2 );
    for (i = 0; i < pfs->total_files; i++)
        if (!strcmp( strings + entries[i].name_offset, "main.npdm" ))
            meta = (const struct npdm_meta *)(files + entries[i].data_offset);
    assert( meta );
    assert( !memcmp( &meta->magic, "META", 4 ) );
    /* The three bits that decide where the program's address space begins. */
    assert( ((meta->flags >> 1) & 7) == address_space );
    aci0 = (const struct npdm_aci0 *)((const u8 *)meta + meta->aci0_offset);
    acid = (const struct npdm_acid *)((const u8 *)meta + meta->acid_offset);
    assert( !memcmp( &aci0->magic, "ACI0", 4 ) && !memcmp( &acid->magic, "ACID", 4 ) );
    assert( aci0->program_id == tid && acid->program_id_min == tid && acid->program_id_max == tid );
}

static void check_control( const u8 *data, size_t size, const char *name, u64 tid )
{
    const struct nca_header *header = nca_of( data );
    u64 start = (u64)header->fs_table[0].media_start_offset * 0x200;
    const struct integrity_level *levels = header->fs_header[0].hash_data.integrity_meta_info.info_level_hash.levels;
    const u8 *base = data + start + levels[5].logical_offset;
    const romfs_header *romfs = (const romfs_header *)base;
    const romfs_dir *root = (const romfs_dir *)(base + romfs->dirTableOff);
    const NacpLanguageEntry *titles = NULL;
    u32 offset;
    int found = 0;

    assert( header->content_type == NCA_CONTENT_CONTROL );
    assert( header->size == size );
    assert( header->fs_header[0].fs_type == NCA_FS_ROMFS );
    for (offset = root->childFile; offset != 0xFFFFFFFFu;)
    {
        const u8 *at = base + romfs->fileTableOff + offset;
        char entry[64] = {0};
        romfs_file file;

        memcpy( &file, at, sizeof(file) );
        memcpy( entry, at + sizeof(file), file.nameLen < sizeof(entry) ? file.nameLen : sizeof(entry) - 1 );
        if (!strcmp( entry, "control.nacp" ))
        {
            titles = (const NacpLanguageEntry *)(base + romfs->fileDataOff + file.dataOff);
            assert( file.dataSize == sizeof(NacpStruct) );
            found |= 1;
        }
        else if (!strcmp( entry, "icon_AmericanEnglish.dat" )) found |= 2;
        offset = file.sibling;
    }
    assert( found == 3 );
    assert( titles && !strcmp( titles[0].name, name ) && !strcmp( titles[15].name, name ) );
    (void)tid;
}

int main( int argc, char **argv )
{
    static const char nro_path[] = "sdmc:/switch/wine/wine-nx-runtime.nro";
    struct wine_nx_forwarder request;
    const char *step = NULL;
    size_t program_size, control_size, meta_size;
    u8 *program, *control, *meta;
    u64 tid;

    if (argc < 4)
    {
        fprintf( stderr, "usage: %s NPDM ICON OUTDIR\n", argv[0] );
        return 2;
    }
    wine_nx_hbl_npdm = load_file( argv[1], &wine_nx_hbl_npdm_size );
    wine_nx_icon_32bit = load_file( argv[2], &wine_nx_icon_32bit_size );
    wine_nx_hbl_main_size = 4096;
    wine_nx_hbl_main = calloc( 1, wine_nx_hbl_main_size );
    snprintf( shim_out_dir, sizeof(shim_out_dir), "%s", argv[3] );

    memset( &request, 0, sizeof(request) );
    request.nro_path = nro_path;
    request.name = "Autorun 32-bit";
    request.author = "Autorun";
    request.address_space = WINE_NX_SPACE_32BIT;
    request.icon = wine_nx_icon_32bit;
    request.icon_size = wine_nx_icon_32bit_size;

    tid = wine_nx_forwarder_title_id( nro_path, NULL );
    assert( (tid >> 56) == 0x05 );
    assert( !(tid & 0xFFF) );
    assert( tid == wine_nx_forwarder_title_id( nro_path, NULL ) );
    assert( tid != wine_nx_forwarder_title_id( "sdmc:/other.nro", NULL ) );

    assert( !wine_nx_forwarder_install( &request, &step ) );
    assert( !step );

    program = read_nca( 1, &program_size );
    control = read_nca( 2, &control_size );
    meta = read_nca( 3, &meta_size );
    check_program( program, program_size, nro_path );
    check_exefs_npdm( program, WINE_NX_SPACE_32BIT, tid );
    check_control( control, control_size, "Autorun 32-bit", tid );
    assert( nca_of( meta )->content_type == NCA_CONTENT_META );
    assert( nca_of( meta )->fs_header[0].fs_type == NCA_FS_PFS0 );
    assert( nca_of( meta )->size == meta_size );

    /* And the default space, which is the only thing that differs. */
    request.address_space = WINE_NX_SPACE_36BIT;
    request.name = "Autorun";
    assert( !wine_nx_forwarder_install( &request, &step ) );
    free( program );
    program = read_nca( 4, &program_size );
    check_exefs_npdm( program, WINE_NX_SPACE_36BIT, tid );
    free( program );
    free( control );
    free( meta );

    puts( "forwarder: title ids, program exefs and romfs, the address space in the NPDM, and the control "
          "romfs passed" );
    return 0;
}
