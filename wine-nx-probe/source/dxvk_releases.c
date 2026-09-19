#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <curl/curl.h>
#include <switch.h>
#include <zlib.h>

#include "dxvk_releases.h"
#include "launcher_settings.h"

extern void wine_nx_runtime_trace( const char *message );

#define DXVK_API_BODY_MAX (24u * 1024u * 1024u)
#define DXVK_ARCHIVE_MAX  (128u * 1024u * 1024u)
#define DXVK_CACHE_SECONDS (24 * 60 * 60)
#define DXVK_GITHUB_PREFIX "https://github.com/doitsujin/dxvk/"

struct buffer
{
    unsigned char *data;
    size_t size, capacity, limit;
};

struct http_progress
{
    dxvk_progress_callback callback;
    void *opaque;
};

static const char *const dxvk_dlls[] =
{
    "d3d8.dll", "d3d9.dll", "d3d10.dll", "d3d10_1.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"
};

static size_t receive_data( void *data, size_t size, size_t count, void *opaque )
{
    struct buffer *buffer = opaque;
    unsigned char *grown;
    size_t bytes, capacity;

    if (count && size > SIZE_MAX / count) return 0;
    bytes = size * count;
    if (bytes > buffer->limit - buffer->size) return 0;
    if (buffer->size + bytes + 1 > buffer->capacity)
    {
        capacity = buffer->capacity ? buffer->capacity : 16384;
        while (capacity < buffer->size + bytes + 1)
        {
            if (capacity > buffer->limit / 2) { capacity = buffer->limit + 1; break; }
            capacity *= 2;
        }
        if (capacity > buffer->limit + 1 || !(grown = realloc( buffer->data, capacity ))) return 0;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy( buffer->data + buffer->size, data, bytes );
    buffer->size += bytes;
    buffer->data[buffer->size] = 0;
    return bytes;
}

static int receive_progress( void *opaque, curl_off_t total, curl_off_t current,
                             curl_off_t upload_total, curl_off_t upload_current )
{
    struct http_progress *progress = opaque;

    (void)upload_total;
    (void)upload_current;
    progress->callback( progress->opaque, DXVK_PROGRESS_DOWNLOAD,
                        current > 0 ? (unsigned long long)current : 0,
                        total > 0 ? (unsigned long long)total : 0 );
    return 0;
}

static enum dxvk_result http_get( const char *url, size_t limit, struct buffer *body,
                                  dxvk_progress_callback callback, void *opaque )
{
    CURL *curl;
    CURLcode code;
    struct curl_slist *headers = NULL;
    struct http_progress progress = { callback, opaque };
    long status = 0;
    char error[CURL_ERROR_SIZE] = "", diagnostic[512];

    memset( body, 0, sizeof(*body) );
    body->limit = limit;
    if (curl_global_init( CURL_GLOBAL_DEFAULT ) != CURLE_OK || !(curl = curl_easy_init()))
        return DXVK_NETWORK_ERROR;
    headers = curl_slist_append( headers, "Accept: application/vnd.github+json" );
    headers = curl_slist_append( headers, "X-GitHub-Api-Version: 2022-11-28" );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, receive_data );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, body );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( curl, CURLOPT_MAXREDIRS, 5L );
    curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 12L );
    curl_easy_setopt( curl, CURLOPT_TIMEOUT, 180L );
    curl_easy_setopt( curl, CURLOPT_LOW_SPEED_LIMIT, 1024L );
    curl_easy_setopt( curl, CURLOPT_LOW_SPEED_TIME, 20L );
    curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );
    curl_easy_setopt( curl, CURLOPT_USERAGENT, "Autorun/DXVK" );
    curl_easy_setopt( curl, CURLOPT_ERRORBUFFER, error );
    if (callback)
    {
        curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 0L );
        curl_easy_setopt( curl, CURLOPT_XFERINFOFUNCTION, receive_progress );
        curl_easy_setopt( curl, CURLOPT_XFERINFODATA, &progress );
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt( curl, CURLOPT_PROTOCOLS_STR, "https" );
    curl_easy_setopt( curl, CURLOPT_REDIR_PROTOCOLS_STR, "https" );
#else
    curl_easy_setopt( curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS );
    curl_easy_setopt( curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS );
#endif
    code = curl_easy_perform( curl );
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &status );
    curl_slist_free_all( headers );
    curl_easy_cleanup( curl );
    if (code == CURLE_OK && status >= 200 && status < 300) return DXVK_OK;
    snprintf( diagnostic, sizeof(diagnostic), "[DXVK] request failed: curl=%d (%s), http=%ld, errno=%d",
              (int)code, error[0] ? error : curl_easy_strerror( code ), status, errno );
    wine_nx_runtime_trace( diagnostic );
    free( body->data );
    memset( body, 0, sizeof(*body) );
    return status == 404 ? DXVK_NOT_FOUND : DXVK_NETWORK_ERROR;
}

static const char *json_field( const char *object, const char *end, const char *name )
{
    char key[64];
    const char *p;
    size_t length;

    snprintf( key, sizeof(key), "\"%s\"", name );
    length = strlen( key );
    for (p = object; p && p < end; p = strstr( p + 1, key ))
    {
        const char *colon;

        if (strncmp( p, key, length )) continue;
        colon = strchr( p + length, ':' );
        if (!colon || colon >= end) return NULL;
        do colon++; while (colon < end && isspace( (unsigned char)*colon ));
        return colon;
    }
    return NULL;
}

static int json_string( const char *p, const char *end, char *out, size_t size )
{
    size_t used = 0;

    if (!p || p >= end || *p++ != '"') return 0;
    while (p < end && *p != '"')
    {
        unsigned char c = *p++;

        if (c == '\\' && p < end)
        {
            c = *p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'u') { if (end - p < 4) return 0; p += 4; c = '?'; }
        }
        if (used + 1 < size) out[used++] = c;
    }
    if (p >= end) return 0;
    out[used] = 0;
    return 1;
}

static int json_integer( const char *p, const char *end, unsigned long long *value )
{
    char *tail;

    if (!p || p >= end) return 0;
    *value = strtoull( p, &tail, 10 );
    return tail != p && tail <= end;
}

static int json_boolean( const char *p, const char *end, int *value )
{
    if (!p || p >= end) return 0;
    if (end - p >= 4 && !memcmp( p, "true", 4 )) { *value = 1; return 1; }
    if (end - p >= 5 && !memcmp( p, "false", 5 )) { *value = 0; return 1; }
    return 0;
}

static int next_object( const char **cursor, const char *end, const char **start, const char **finish )
{
    const char *p = *cursor;
    int depth = 0, quoted = 0, escaped = 0;

    while (p < end && *p != '{' && *p != ']') p++;
    if (p == end || *p == ']') return 0;
    *start = p;
    for (; p < end; p++)
    {
        char c = *p;

        if (quoted)
        {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') quoted = 0;
        }
        else if (c == '"') quoted = 1;
        else if (c == '{') depth++;
        else if (c == '}' && --depth == 0)
        {
            *finish = p + 1;
            *cursor = p + 1;
            return 1;
        }
    }
    return 0;
}

static int json_array( const char *object, const char *end, const char *name,
                       const char **start, const char **finish )
{
    const char *p = json_field( object, end, name );
    int depth = 0, quoted = 0, escaped = 0;

    if (!p || p >= end || *p != '[') return 0;
    *start = p + 1;
    for (; p < end; p++)
    {
        char c = *p;

        if (quoted)
        {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') quoted = 0;
        }
        else if (c == '"') quoted = 1;
        else if (c == '[') depth++;
        else if (c == ']' && --depth == 0) { *finish = p; return 1; }
    }
    return 0;
}

static int parse_release_page( const unsigned char *json, size_t size, struct dxvk_release *releases,
                               int max_releases, int *seen )
{
    const char *begin = (const char *)json, *end = begin + size, *cursor, *object, *object_end;
    int added = 0;

    cursor = memchr( begin, '[', size );
    if (!cursor) return -1;
    cursor++;
    while (next_object( &cursor, end, &object, &object_end ))
    {
        const char *assets, *assets_end, *asset_cursor, *asset, *asset_end;
        struct dxvk_release release;
        char tag[40], asset_name[80], expected[80], digest[80];
        int draft = 0;

        (*seen)++;
        memset( &release, 0, sizeof(release) );
        if (!json_string( json_field( object, object_end, "tag_name" ), object_end, tag, sizeof(tag) ) ||
            !json_boolean( json_field( object, object_end, "draft" ), object_end, &draft ) || draft)
            continue;
        json_boolean( json_field( object, object_end, "prerelease" ), object_end, &release.prerelease );
        if (tag[0] == 'v' || tag[0] == 'V') memmove( tag, tag + 1, strlen( tag ) );
        if (!launcher_dxvk_version_valid( tag )) continue;
        memcpy( release.version, tag, strlen( tag ) + 1 );
        snprintf( expected, sizeof(expected), "dxvk-%s.tar.gz", tag );
        if (!json_array( object, object_end, "assets", &assets, &assets_end )) continue;
        asset_cursor = assets;
        while (next_object( &asset_cursor, assets_end, &asset, &asset_end ))
        {
            if (!json_string( json_field( asset, asset_end, "name" ), asset_end,
                              asset_name, sizeof(asset_name) ) || strcmp( asset_name, expected )) continue;
            if (!json_string( json_field( asset, asset_end, "browser_download_url" ), asset_end,
                              release.url, sizeof(release.url) )) break;
            json_integer( json_field( asset, asset_end, "size" ), asset_end, &release.size );
            if (json_string( json_field( asset, asset_end, "digest" ), asset_end, digest, sizeof(digest) ) &&
                !strncmp( digest, "sha256:", 7 ) && strlen( digest + 7 ) == 64)
                snprintf( release.digest, sizeof(release.digest), "%s", digest + 7 );
            break;
        }
        if (!release.url[0]) continue;
        if (added < max_releases) releases[added++] = release;
    }
    return added;
}

static int make_directory( const char *path )
{
    if (!mkdir( path, 0777 ) || errno == EEXIST) return 1;
    return 0;
}

static int make_directories( const char *path )
{
    char copy[768], *p;

    if (strlen( path ) >= sizeof(copy)) return 0;
    strcpy( copy, path );
    for (p = strchr( copy, '/' ); p; p = strchr( p + 1, '/' ))
    {
        if (p == copy || p[-1] == ':') continue;
        *p = 0;
        if (!make_directory( copy )) return 0;
        *p = '/';
    }
    return make_directory( copy );
}

static int remove_tree( const char *path )
{
    DIR *directory = opendir( path );
    struct dirent *entry;
    int ok = 1;

    if (!directory) return errno == ENOENT;
    while ((entry = readdir( directory )))
    {
        char child[896];
        struct stat st;

        if (!strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." )) continue;
        if ((size_t)snprintf( child, sizeof(child), "%s/%s", path, entry->d_name ) >= sizeof(child) ||
            lstat( child, &st )) { ok = 0; continue; }
        if (S_ISDIR( st.st_mode )) { if (!remove_tree( child )) ok = 0; }
        else if (remove( child )) ok = 0;
    }
    closedir( directory );
    if (rmdir( path )) ok = 0;
    return ok;
}

static int write_atomic( const char *path, const void *data, size_t size )
{
    char temp[896];
    FILE *file;
    int ok;

    if ((size_t)snprintf( temp, sizeof(temp), "%s.new", path ) >= sizeof(temp) || !(file = fopen( temp, "wb" )))
        return 0;
    ok = fwrite( data, 1, size, file ) == size && !fflush( file );
    if (fclose( file )) ok = 0;
    if (ok) remove( path );
    if (!ok || rename( temp, path )) { remove( temp ); return 0; }
    return 1;
}

static int cache_path( const char *runtime_dir, char *folder, size_t folder_size, char *path, size_t path_size )
{
    int folder_length = snprintf( folder, folder_size, "%s/cache", runtime_dir );
    int path_length;

    if (folder_length <= 0 || (size_t)folder_length >= folder_size) return 0;
    path_length = snprintf( path, path_size, "%s/dxvk-releases.txt", folder );
    return path_length > 0 && (size_t)path_length < path_size;
}

static int save_catalog( const char *runtime_dir, const struct dxvk_release *releases, int count )
{
    char folder[768], path[800], temp[808];
    FILE *file;
    int i, ok = 1;

    if (!cache_path( runtime_dir, folder, sizeof(folder), path, sizeof(path) ) || !make_directories( folder ) ||
        (size_t)snprintf( temp, sizeof(temp), "%s.new", path ) >= sizeof(temp) || !(file = fopen( temp, "wb" )))
        return 0;
    for (i = 0; i < count; i++)
        if (fprintf( file, "%s\t%d\t%llu\t%s\t%s\n", releases[i].version, releases[i].prerelease,
                     releases[i].size, releases[i].digest, releases[i].url ) < 0) { ok = 0; break; }
    if (fclose( file )) ok = 0;
    if (ok) remove( path );
    if (!ok || rename( temp, path )) { remove( temp ); return 0; }
    return 1;
}

static int split_field( char **cursor, char **value )
{
    char *start = *cursor, *tab;

    if (!start) return 0;
    tab = strchr( start, '\t' );
    if (tab) { *tab = 0; *cursor = tab + 1; }
    else *cursor = NULL;
    *value = start;
    return 1;
}

static int load_catalog( const char *runtime_dir, struct dxvk_release *releases, int max_releases,
                         int *count, int *fresh )
{
    char folder[768], path[800], line[1024];
    struct stat st;
    FILE *file;
    int n = 0;
    time_t now = time( NULL );

    *fresh = 0;
    if (!cache_path( runtime_dir, folder, sizeof(folder), path, sizeof(path) ) || stat( path, &st ) ||
        !(file = fopen( path, "rb" ))) return 0;
    if (now >= st.st_mtime && now - st.st_mtime < DXVK_CACHE_SECONDS) *fresh = 1;
    while (n < max_releases && fgets( line, sizeof(line), file ))
    {
        struct dxvk_release release;
        char *cursor = line, *version, *prerelease, *size, *digest, *url, *end;

        memset( &release, 0, sizeof(release) );
        if (!split_field( &cursor, &version ) || !split_field( &cursor, &prerelease ) ||
            !split_field( &cursor, &size ) || !split_field( &cursor, &digest ) ||
            !split_field( &cursor, &url )) continue;
        url[strcspn( url, "\r\n" )] = 0;
        if (!launcher_dxvk_version_valid( version ) ||
            strncmp( url, DXVK_GITHUB_PREFIX, strlen(DXVK_GITHUB_PREFIX) )) continue;
        release.prerelease = strtol( prerelease, &end, 10 );
        if (*end || (release.prerelease != 0 && release.prerelease != 1)) continue;
        release.size = strtoull( size, &end, 10 );
        if (*end || (digest[0] && strlen( digest ) != 64)) continue;
        memcpy( release.version, version, strlen( version ) + 1 );
        snprintf( release.digest, sizeof(release.digest), "%s", digest );
        snprintf( release.url, sizeof(release.url), "%s", url );
        releases[n++] = release;
    }
    fclose( file );
    *count = n;
    return n > 0;
}

enum dxvk_result dxvk_release_catalog( const char *runtime_dir, struct dxvk_release *releases,
                                       int max_releases, int *count, int refresh, int *cached )
{
    struct dxvk_release old[DXVK_MAX_RELEASES];
    int old_count = 0, fresh = 0, total = 0, page;

    if (!runtime_dir || !releases || max_releases <= 0 || !count) return DXVK_INVALID_RESPONSE;
    if (max_releases > DXVK_MAX_RELEASES) max_releases = DXVK_MAX_RELEASES;
    if (load_catalog( runtime_dir, releases, max_releases, count, &fresh ) && fresh && !refresh)
    {
        if (cached) *cached = 1;
        return DXVK_OK;
    }
    load_catalog( runtime_dir, old, DXVK_MAX_RELEASES, &old_count, &fresh );
    for (page = 1; total < max_releases; page++)
    {
        char url[160];
        struct buffer body;
        enum dxvk_result result;
        int seen = 0, added;

        snprintf( url, sizeof(url),
                  "https://api.github.com/repos/doitsujin/dxvk/releases?per_page=100&page=%d", page );
        if ((result = http_get( url, DXVK_API_BODY_MAX, &body, NULL, NULL )) != DXVK_OK)
        {
            if (old_count)
            {
                memcpy( releases, old, old_count * sizeof(*old) );
                *count = old_count;
                if (cached) *cached = 1;
                return DXVK_OK;
            }
            return result;
        }
        added = parse_release_page( body.data, body.size, releases + total, max_releases - total, &seen );
        free( body.data );
        if (added < 0) return DXVK_INVALID_RESPONSE;
        total += added;
        if (seen < 100 || page >= 4) break;
    }
    if (!total) return DXVK_NOT_FOUND;
    *count = total;
    if (cached) *cached = 0;
    save_catalog( runtime_dir, releases, total );
    return DXVK_OK;
}

static int approved_dll( const char *name )
{
    size_t i;

    for (i = 0; i < sizeof(dxvk_dlls) / sizeof(dxvk_dlls[0]); i++)
        if (!strcasecmp( name, dxvk_dlls[i] )) return 1;
    return 0;
}

static unsigned long long tar_size( const unsigned char *field, size_t length, int *ok )
{
    unsigned long long value = 0;
    size_t i = 0;

    while (i < length && (field[i] == ' ' || field[i] == 0)) i++;
    for (; i < length && field[i]; i++)
    {
        if (field[i] == ' ') break;
        if (field[i] < '0' || field[i] > '7') { *ok = 0; return 0; }
        if (value > (UINT64_MAX - 7) / 8) { *ok = 0; return 0; }
        value = value * 8 + field[i] - '0';
    }
    return value;
}

static int read_gz( gzFile archive, void *data, size_t size )
{
    unsigned char *out = data;

    while (size)
    {
        unsigned int part = size > 0x40000000u ? 0x40000000u : (unsigned int)size;
        int read = gzread( archive, out, part );

        if (read <= 0) return 0;
        out += read;
        size -= read;
    }
    return 1;
}

static int discard_gz( gzFile archive, unsigned long long size )
{
    unsigned char buffer[32768];

    while (size)
    {
        size_t part = size < sizeof(buffer) ? (size_t)size : sizeof(buffer);

        if (!read_gz( archive, buffer, part )) return 0;
        size -= part;
    }
    return 1;
}

static int extract_archive( const char *archive_path, const char *x32, const char *x64,
                            int *x32_files, int *x64_files )
{
    unsigned char header[512], buffer[65536];
    gzFile archive = gzopen( archive_path, "rb" );
    int zero_blocks = 0, ok = 1;

    if (!archive) return 0;
    while (read_gz( archive, header, sizeof(header) ))
    {
        char name[101], *slash, *arch;
        const char *destination = NULL;
        unsigned long long size, padded, left;
        FILE *output = NULL;
        int field_ok = 1, regular;

        if (!memcmp( header, (unsigned char[512]){0}, 512))
        {
            if (++zero_blocks == 2) break;
            continue;
        }
        zero_blocks = 0;
        memcpy( name, header, 100 );
        name[100] = 0;
        if (!memchr( header, 0, 100)) { ok = 0; break; }
        size = tar_size( header + 124, 12, &field_ok );
        if (!field_ok || size > 64u * 1024u * 1024u) { ok = 0; break; }
        regular = !header[156] || header[156] == '0';
        slash = strrchr( name, '/' );
        if (regular && slash && approved_dll( slash + 1 ))
        {
            *slash = 0;
            arch = strrchr( name, '/' );
            arch = arch ? arch + 1 : name;
            if (!strcmp( arch, "x32" )) destination = x32;
            else if (!strcmp( arch, "x64" )) destination = x64;
            *slash = '/';
        }
        if (destination)
        {
            char output_path[896];

            if ((size_t)snprintf( output_path, sizeof(output_path), "%s/%s", destination, slash + 1 ) >=
                sizeof(output_path) || !(output = fopen( output_path, "wb" ))) { ok = 0; break; }
        }
        left = size;
        while (left)
        {
            size_t part = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);

            if (!read_gz( archive, buffer, part ) || (output && fwrite( buffer, 1, part, output ) != part))
            { ok = 0; break; }
            left -= part;
        }
        if (output && fclose( output )) ok = 0;
        if (!ok) break;
        if (destination)
        {
            if (destination == x32) (*x32_files)++;
            else (*x64_files)++;
        }
        padded = (512 - size % 512) % 512;
        if (padded && !discard_gz( archive, padded )) { ok = 0; break; }
    }
    if (gzclose( archive ) != Z_OK) ok = 0;
    return ok;
}

static int valid_pe( const char *path, unsigned short machine )
{
    unsigned char dos[64], header[26];
    unsigned int offset;
    unsigned short actual, characteristics, magic;
    FILE *file = fopen( path, "rb" );

    if (!file) return 0;
    if (fread( dos, 1, sizeof(dos), file ) != sizeof(dos) || dos[0] != 'M' || dos[1] != 'Z')
    { fclose( file ); return 0; }
    offset = dos[0x3c] | dos[0x3d] << 8 | dos[0x3e] << 16 | dos[0x3f] << 24;
    if (offset < 64 || fseek( file, offset, SEEK_SET ) || fread( header, 1, sizeof(header), file ) != sizeof(header))
    { fclose( file ); return 0; }
    fclose( file );
    actual = header[4] | header[5] << 8;
    characteristics = header[22] | header[23] << 8;
    magic = header[24] | header[25] << 8;
    return !memcmp( header, "PE\0\0", 4 ) && actual == machine &&
           magic == (machine == 0x8664 ? 0x20b : 0x10b) && (characteristics & 0x2000);
}

static int validate_payload( const char *path, unsigned short machine )
{
    size_t i;
    int valid = 0;

    for (i = 0; i < sizeof(dxvk_dlls) / sizeof(dxvk_dlls[0]); i++)
    {
        char dll[896];
        struct stat st;

        if ((size_t)snprintf( dll, sizeof(dll), "%s/%s", path, dxvk_dlls[i] ) >= sizeof(dll) ||
            stat( dll, &st )) continue;
        if (!S_ISREG( st.st_mode ) || !valid_pe( dll, machine )) return 0;
        valid++;
    }
    return valid > 0;
}

static int write_release_info( const char *path, const struct dxvk_release *release )
{
    char info[1024], file[896];
    int length;

    length = snprintf( info, sizeof(info), "version=%s\nsource=%s\nsha256=%s\n",
                       release->version, release->url, release->digest );
    if (length <= 0 || (size_t)length >= sizeof(info) ||
        (size_t)snprintf( file, sizeof(file), "%s/release.ini", path ) >= sizeof(file)) return 0;
    return write_atomic( file, info, length );
}

static int verify_digest( const struct buffer *body, const char *expected )
{
    unsigned char digest[SHA256_HASH_SIZE];
    char hex[SHA256_HASH_SIZE * 2 + 1];
    static const char digits[] = "0123456789abcdef";
    size_t i;

    if (!expected[0]) return 1;
    sha256CalculateHash( digest, body->data, body->size );
    for (i = 0; i < sizeof(digest); i++)
    {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[sizeof(hex) - 1] = 0;
    return !strcasecmp( hex, expected );
}

enum dxvk_result dxvk_install_release( const char *runtime_dir, const struct dxvk_release *release,
                                       dxvk_progress_callback progress, void *opaque )
{
    char cache[768], archive_path[896], x32_base[768], x64_base[768];
    char x32_new[896], x64_new[896], x32_final[896], x64_final[896];
    struct buffer body;
    enum dxvk_result result;
    int x32_files = 0, x64_files = 0;

    if (!runtime_dir || !release || !launcher_dxvk_version_valid( release->version ) ||
        strncmp( release->url, DXVK_GITHUB_PREFIX, strlen(DXVK_GITHUB_PREFIX) )) return DXVK_INVALID_RESPONSE;
    if ((size_t)snprintf( cache, sizeof(cache), "%s/cache", runtime_dir ) >= sizeof(cache) ||
        !make_directories( cache ) ||
        (size_t)snprintf( archive_path, sizeof(archive_path), "%s/dxvk-%s.download", cache,
                          release->version ) >= sizeof(archive_path) ||
        (size_t)snprintf( x32_base, sizeof(x32_base), "%s/drive_c/dxvk/versions", runtime_dir ) >= sizeof(x32_base) ||
        (size_t)snprintf( x64_base, sizeof(x64_base), "%s/drive_c/dxvk64/versions", runtime_dir ) >= sizeof(x64_base) ||
        !make_directories( x32_base ) || !make_directories( x64_base )) return DXVK_IO_ERROR;
    snprintf( x32_new, sizeof(x32_new), "%s/%s.new", x32_base, release->version );
    snprintf( x64_new, sizeof(x64_new), "%s/%s.new", x64_base, release->version );
    snprintf( x32_final, sizeof(x32_final), "%s/%s", x32_base, release->version );
    snprintf( x64_final, sizeof(x64_final), "%s/%s", x64_base, release->version );
    remove_tree( x32_new );
    remove_tree( x64_new );
    if (!make_directory( x32_new ) || !make_directory( x64_new )) return DXVK_IO_ERROR;
    if (progress) progress( opaque, DXVK_PROGRESS_DOWNLOAD, 0, release->size );
    if ((result = http_get( release->url, DXVK_ARCHIVE_MAX, &body, progress, opaque )) != DXVK_OK) goto failed;
    if (progress) progress( opaque, DXVK_PROGRESS_VERIFY, body.size, body.size );
    if (body.size < 2 || body.data[0] != 0x1f || body.data[1] != 0x8b)
    { result = DXVK_INVALID_ARCHIVE; goto free_failed; }
    if (!verify_digest( &body, release->digest )) { result = DXVK_HASH_MISMATCH; goto free_failed; }
    if (!write_atomic( archive_path, body.data, body.size )) { result = DXVK_IO_ERROR; goto free_failed; }
    free( body.data );
    memset( &body, 0, sizeof(body) );
    if (progress) progress( opaque, DXVK_PROGRESS_INSTALL, 0, 0 );
    if (!extract_archive( archive_path, x32_new, x64_new, &x32_files, &x64_files ) ||
        !x32_files || !x64_files || !validate_payload( x32_new, 0x014c ) ||
        !validate_payload( x64_new, 0x8664 )) { result = DXVK_INVALID_ARCHIVE; goto failed; }
    if (!write_release_info( x32_new, release ) || !write_release_info( x64_new, release ))
    { result = DXVK_IO_ERROR; goto failed; }
    remove_tree( x32_final );
    remove_tree( x64_final );
    if (rename( x32_new, x32_final ) || rename( x64_new, x64_final ))
    { result = DXVK_IO_ERROR; goto failed; }
    remove( archive_path );
    return DXVK_OK;

free_failed:
    free( body.data );
failed:
    remove( archive_path );
    remove_tree( x32_new );
    remove_tree( x64_new );
    return result;
}

static int payload_path( const char *runtime_dir, unsigned short machine, const char *version,
                         char *path, size_t size )
{
    const char *base = launcher_dxvk_directory( machine );
    int length;

    if (!base || (version && version[0] && !launcher_dxvk_version_valid( version ))) return 0;
    if (version && version[0])
        length = snprintf( path, size, "%s/drive_c/%s/versions/%s", runtime_dir, base, version );
    else length = snprintf( path, size, "%s/drive_c/%s", runtime_dir, base );
    return length >= 0 && (size_t)length < size;
}

int dxvk_release_installed( const char *runtime_dir, unsigned short machine, const char *version )
{
    char path[896];

    return payload_path( runtime_dir, machine, version, path, sizeof(path) ) && validate_payload( path, machine );
}

int dxvk_root_version( const char *runtime_dir, unsigned short machine, char *version, size_t size )
{
    char root[896], path[920], text[2048];
    const char *field, *colon, *start, *end;
    FILE *file;
    size_t read, length;

    if (!version || !size || !payload_path( runtime_dir, machine, "", root, sizeof(root) ) ||
        (size_t)snprintf( path, sizeof(path), "%s/dxvk-manifest.json", root ) >= sizeof(path) ||
        !(file = fopen( path, "rb" ))) return 0;
    read = fread( text, 1, sizeof(text) - 1, file );
    fclose( file );
    text[read] = 0;
    if (!(field = strstr( text, "\"version\"" )) || !(colon = strchr( field + 9, ':' )) ||
        !(start = strchr( colon + 1, '"' ))) return 0;
    start++;
    if (!(end = strchr( start, '"' ))) return 0;
    length = end - start;
    if (!length || length >= size || length >= 32) return 0;
    memcpy( version, start, length );
    version[length] = 0;
    return launcher_dxvk_version_valid( version );
}

const char *dxvk_result_message( enum dxvk_result result )
{
    switch (result)
    {
    case DXVK_OK: return "DXVK is ready.";
    case DXVK_NETWORK_ERROR: return "Could not connect to GitHub.";
    case DXVK_NOT_FOUND: return "No official DXVK release asset was found.";
    case DXVK_INVALID_RESPONSE: return "GitHub returned an invalid release catalog.";
    case DXVK_INVALID_ARCHIVE: return "The downloaded DXVK archive is invalid.";
    case DXVK_HASH_MISMATCH: return "The downloaded DXVK archive failed SHA-256 verification.";
    case DXVK_IO_ERROR: return "DXVK could not be written to the SD card.";
    }
    return "DXVK failed.";
}
