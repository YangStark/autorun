/*
 * Settings files of the launcher: lines of key=value, with other lines (comments,
 * keys a later build adds) kept as they are when a value is changed.
 *
 * A program's own settings live next to it, named after it: SPEED2.EXE reads
 * SPEED2.wine-nx.txt. The launcher writes them and the runtime applies them to
 * the program it starts, whether the launcher chose it or target.txt did.
 * The launcher's own look is in sdmc:/switch/wine/launcher.txt.
 */
#ifndef WINE_NX_LAUNCHER_SETTINGS_H
#define WINE_NX_LAUNCHER_SETTINGS_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LAUNCHER_KV_MAX 8192

struct launcher_kv
{
    char text[LAUNCHER_KV_MAX];
    size_t size;
};

/* A missing file reads as empty. Returns 0 when the file is too large to keep whole. */
static inline int launcher_kv_load( struct launcher_kv *kv, const char *path )
{
    FILE *file = fopen( path, "rb" );
    int ok = 1;

    kv->size = 0;
    kv->text[0] = 0;
    if (!file) return 1;
    kv->size = fread( kv->text, 1, sizeof(kv->text) - 1, file );
    if (kv->size == sizeof(kv->text) - 1 && fgetc( file ) != EOF) ok = 0;
    fclose( file );
    kv->text[kv->size] = 0;
    return ok;
}

/* Find key's line: *line and *end bound it (without its newline). */
static inline int launcher_kv_find( const struct launcher_kv *kv, const char *key, size_t *line, size_t *end )
{
    size_t len = strlen( key ), pos = 0;

    while (pos < kv->size)
    {
        size_t start = pos, p = pos, stop;

        while (pos < kv->size && kv->text[pos] != '\n') pos++;
        stop = pos;
        if (pos < kv->size) pos++;
        while (p < stop && (kv->text[p] == ' ' || kv->text[p] == '\t')) p++;
        if (stop - p < len || strncasecmp( kv->text + p, key, len )) continue;
        p += len;
        while (p < stop && (kv->text[p] == ' ' || kv->text[p] == '\t')) p++;
        if (p < stop && kv->text[p] == '=')
        {
            *line = start;
            *end = stop;
            return 1;
        }
    }
    return 0;
}

/* The value of key with surrounding spaces and a carriage return removed, or NULL. */
static inline const char *launcher_kv_get( const struct launcher_kv *kv, const char *key, char *out, size_t size )
{
    size_t line, end, p, len;

    if (!size || !launcher_kv_find( kv, key, &line, &end )) return NULL;
    p = (char *)memchr( kv->text + line, '=', end - line ) - kv->text + 1;
    while (p < end && (kv->text[p] == ' ' || kv->text[p] == '\t')) p++;
    while (end > p && (kv->text[end - 1] == ' ' || kv->text[end - 1] == '\t' || kv->text[end - 1] == '\r')) end--;
    len = end - p < size - 1 ? end - p : size - 1;
    memcpy( out, kv->text + p, len );
    out[len] = 0;
    return out;
}

/* An integer value, or fallback when the key is missing or not a number. */
static inline int launcher_kv_get_int( const struct launcher_kv *kv, const char *key, int fallback )
{
    char value[32], *end;
    long n;

    if (!launcher_kv_get( kv, key, value, sizeof(value) ) || !value[0]) return fallback;
    n = strtol( value, &end, 10 );
    return *end ? fallback : (int)n;
}

/* Set key to value in place, add it at the end, or remove its line when value is NULL.
 * Returns 0 when the result would not fit. */
static inline int launcher_kv_set( struct launcher_kv *kv, const char *key, const char *value )
{
    char line[1024];
    size_t start, end, len = 0;

    if (value)
    {
        int n = snprintf( line, sizeof(line), "%s=%s", key, value );

        if (n < 0 || (size_t)n >= sizeof(line) || strpbrk( value, "\r\n" )) return 0;
        len = n;
    }
    if (launcher_kv_find( kv, key, &start, &end ))
    {
        if (!value && end < kv->size) end++;  /* the newline goes too */
    }
    else
    {
        if (!value) return 1;
        start = end = kv->size;
        if (kv->size && kv->text[kv->size - 1] != '\n')
        {
            if (kv->size + 1 >= sizeof(kv->text)) return 0;
            kv->text[kv->size++] = '\n';
            start = end = kv->size;
        }
        if (kv->size + len + 1 >= sizeof(kv->text)) return 0;
        line[len++] = '\n';
    }
    if (kv->size - (end - start) + len >= sizeof(kv->text)) return 0;
    memmove( kv->text + start + len, kv->text + end, kv->size - end );
    memcpy( kv->text + start, line, len );
    kv->size = kv->size - (end - start) + len;
    kv->text[kv->size] = 0;
    return 1;
}

/* Whether any line holds more than spaces. */
static inline int launcher_kv_empty( const struct launcher_kv *kv )
{
    size_t i;

    for (i = 0; i < kv->size; i++)
        if (!isspace( (unsigned char)kv->text[i] )) return 0;
    return 1;
}

/* Write the file through a temporary one, so a failed write keeps the old file;
 * a file with nothing left in it is removed. */
static inline int launcher_kv_save( const struct launcher_kv *kv, const char *path )
{
    char temp[768];
    FILE *file;
    int ok;

    if (launcher_kv_empty( kv ))
    {
        if (!remove( path ) || !(file = fopen( path, "rb" ))) return 1;
        fclose( file );
        return 0;
    }
    if ((size_t)snprintf( temp, sizeof(temp), "%s.new", path ) >= sizeof(temp)) return 0;
    if (!(file = fopen( temp, "wb" ))) return 0;
    ok = fwrite( kv->text, 1, kv->size, file ) == kv->size;
    ok = !fclose( file ) && ok;
    /* FAT cannot rename over an existing file. */
    if (ok) remove( path );
    if (!ok || rename( temp, path ))
    {
        remove( temp );
        return 0;
    }
    return 1;
}

/* The settings of one program. -1 means the global setting applies. */
struct launcher_settings
{
    char title[128];  /* empty: the title from the program's resources */
    int hidden;       /* left out of the library */
    int verbose;      /* verbose traces */
    int profile;      /* the sampling profiler */
    int framebuffer;  /* 1: windows go to the framebuffer, 0: through the compositor */
    int dxvk;         /* 1: Direct3D 9 from C:\dxvk\d3d9.dll, 0: Wine's */
};

static inline int launcher_settings_path( const char *exe_path, char *out, size_t size )
{
    size_t len = strlen( exe_path ), suffix_size = sizeof(".wine-nx.txt");

    if (len < 4 || strcasecmp( exe_path + len - 4, ".exe" ) || len - 4 + suffix_size > size) return 0;
    memcpy( out, exe_path, len - 4 );
    memcpy( out + len - 4, ".wine-nx.txt", suffix_size );
    return 1;
}

static inline int launcher_setting_state( const struct launcher_kv *kv, const char *key )
{
    char value[16];

    if (!launcher_kv_get( kv, key, value, sizeof(value) )) return -1;
    if (!strcmp( value, "1" ) || !strcasecmp( value, "on" )) return 1;
    if (!strcmp( value, "0" ) || !strcasecmp( value, "off" )) return 0;
    return -1;
}

static inline void launcher_settings_read( const struct launcher_kv *kv, struct launcher_settings *settings )
{
    char value[16];

    if (!launcher_kv_get( kv, "title", settings->title, sizeof(settings->title) )) settings->title[0] = 0;
    settings->hidden = launcher_setting_state( kv, "hidden" ) == 1;
    settings->verbose = launcher_setting_state( kv, "verbose" );
    settings->profile = launcher_setting_state( kv, "profile" );
    settings->framebuffer = -1;
    if (launcher_kv_get( kv, "windows", value, sizeof(value) ))
    {
        if (!strcasecmp( value, "framebuffer" )) settings->framebuffer = 1;
        else if (!strcasecmp( value, "compositor" )) settings->framebuffer = 0;
    }
    settings->dxvk = launcher_kv_get( kv, "d3d9", value, sizeof(value) ) && !strcasecmp( value, "dxvk" );
}

/* Store settings, leaving out what matches the global settings. */
static inline int launcher_settings_write( struct launcher_kv *kv, const struct launcher_settings *settings )
{
    static const char *states[] = { NULL, "0", "1" };

    return launcher_kv_set( kv, "title", settings->title[0] ? settings->title : NULL ) &&
           launcher_kv_set( kv, "hidden", settings->hidden ? "1" : NULL ) &&
           launcher_kv_set( kv, "verbose", states[settings->verbose + 1] ) &&
           launcher_kv_set( kv, "profile", states[settings->profile + 1] ) &&
           launcher_kv_set( kv, "windows", settings->framebuffer < 0 ? NULL :
                                           settings->framebuffer ? "framebuffer" : "compositor" ) &&
           launcher_kv_set( kv, "d3d9", settings->dxvk ? "dxvk" : NULL );
}

#endif
