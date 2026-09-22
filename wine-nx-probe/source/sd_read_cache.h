/*
 * A read cache for files on the SD card, in front of libnx's sdmc device.
 *
 * libnx turns each read() into an fsFileRead request to the FS service. Horizon
 * has no page cache or file mapping, and libnx has no file data cache (Nintendo's
 * SDK keeps one in its fs client library). OpenTTD reads its graphics with a
 * seek and a small read per sprite, so most requests fetch data that a nearby
 * read already had. As Dolphin's SectorReader does for disc images, each file
 * keeps a few aligned chunks: a read inside one is a copy, a miss reads the
 * whole chunk in one request, and the least recently used chunk is replaced.
 *
 * Only files opened read-only are cached. Opening a path for writing stops
 * caching of every open file with that path, and a file opened while a writer
 * has its path open is not cached. The caller locks around these functions.
 */
#ifndef WINE_NX_SD_READ_CACHE_H
#define WINE_NX_SD_READ_CACHE_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SD_CACHE_CHUNK  (128 * 1024)  /* bytes read per request on a miss */
#define SD_CACHE_LINES  8             /* chunks kept per file */
#define SD_CACHE_DIRECT (64 * 1024)   /* reads this large go straight to the file */
#define SD_CACHE_BYPASS (-2)          /* sd_cache_read found no memory: read directly */

struct sd_cache_line
{
    char *data;
    long long start;   /* file offset of data[0], or -1 while it holds nothing */
    size_t len;        /* bytes held; less than a chunk only at the end of the file */
    unsigned int lru;  /* high bit set on use, shifted down on every miss */
    unsigned long long used_at;  /* the pool's clock when last read */
};

/* When every chunk is in use, a file takes the one read longest ago by any
 * file. Letting a full pool refuse new files instead left everything the
 * first few files had taken with them for as long as they were open: The
 * Sims 2 keeps hundreds of packages open, filled the pool in its first
 * seconds, and read every other package from the card for the rest of the
 * run -- about eight reads a chunk while there was room, then 149,000 card
 * reads in five minutes. held lists the lines holding a chunk. */
struct sd_cache_pool
{
    unsigned int used, max;        /* chunks allocated for all files, and the limit */
    unsigned long long clock;      /* counts every chunk read, across files */
    struct sd_cache_line **held;   /* the lines holding a chunk, used of them */
};

static inline void sd_cache_held_add( struct sd_cache_pool *pool, struct sd_cache_line *line )
{
    pool->held[pool->used++] = line;
}

static inline void sd_cache_held_remove( struct sd_cache_pool *pool, struct sd_cache_line *line )
{
    unsigned int i;

    for (i = 0; i < pool->used; i++)
    {
        if (pool->held[i] != line) continue;
        pool->held[i] = pool->held[--pool->used];
        break;
    }
    if (!pool->used)
    {
        free( pool->held );
        pool->held = NULL;
    }
}

/* A chunk changes hands without the count changing: removing it first would
 * free the list when it held the only one. */
static inline void sd_cache_held_replace( struct sd_cache_pool *pool, struct sd_cache_line *old,
                                          struct sd_cache_line *line )
{
    unsigned int i;

    for (i = 0; i < pool->used; i++)
    {
        if (pool->held[i] != old) continue;
        pool->held[i] = line;
        return;
    }
}

/* The chunk read longest ago, by any file. */
static inline struct sd_cache_line *sd_cache_oldest( struct sd_cache_pool *pool )
{
    struct sd_cache_line *oldest = NULL;
    unsigned int i;

    for (i = 0; i < pool->used; i++)
        if (!oldest || pool->held[i]->used_at < oldest->used_at) oldest = pool->held[i];
    return oldest;
}

struct sd_cache_file
{
    void *key;         /* libnx's per-open file data */
    char *path;
    int writable;
    int cacheable;
    struct sd_cache_line lines[SD_CACHE_LINES];
    struct sd_cache_file *next;
};

/* Reads size bytes at offset into buf. Returns the bytes read or -1. */
typedef long long (*sd_cache_fill_fn)( void *ctx, long long offset, char *buf, size_t size );

static inline void sd_cache_drop( struct sd_cache_file *file, struct sd_cache_pool *pool )
{
    unsigned int i;

    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        if (!file->lines[i].data) continue;
        free( file->lines[i].data );
        sd_cache_held_remove( pool, &file->lines[i] );
        memset( &file->lines[i], 0, sizeof(file->lines[i]) );
    }
}

static inline struct sd_cache_line *sd_cache_line_for( struct sd_cache_file *file, long long offset )
{
    unsigned int i;

    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        struct sd_cache_line *line = &file->lines[i];

        if (line->data && line->start >= 0 && offset >= line->start &&
            offset < line->start + (long long)line->len)
        {
            line->lru |= 0x80000000u;
            return line;
        }
    }
    return NULL;
}

/* A short chunk at chunk_start marks the end of the file. */
static inline int sd_cache_known_end( const struct sd_cache_file *file, long long chunk_start )
{
    unsigned int i;

    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        const struct sd_cache_line *line = &file->lines[i];

        if (line->data && line->start == chunk_start && line->len < SD_CACHE_CHUNK) return 1;
    }
    return 0;
}

/* The line to fill: a buffer holding nothing, then a free slot, then the
 * least recently used chunk. */
static inline struct sd_cache_line *sd_cache_victim( struct sd_cache_file *file )
{
    struct sd_cache_line *victim = &file->lines[0];
    unsigned int i, rank, best = 3;

    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        struct sd_cache_line *line = &file->lines[i];

        line->lru >>= 1;
        rank = !line->data ? 1 : line->start < 0 ? 0 : 2;
        if (rank < best || (rank == 2 && best == 2 && line->lru < victim->lru))
        {
            victim = line;
            best = rank;
        }
    }
    victim->lru = 0x80000000u;
    return victim;
}

/* Copy size bytes at offset into buf, filling chunks through fill. Returns the
 * bytes copied (fewer only at the end of the file), -1 when a request fails
 * before anything was copied, or SD_CACHE_BYPASS when no chunk can be
 * allocated. *fills counts the requests made. */
static inline long long sd_cache_read( struct sd_cache_file *file, struct sd_cache_pool *pool, long long offset,
                                       char *buf, size_t size, sd_cache_fill_fn fill, void *ctx,
                                       unsigned int *fills )
{
    size_t done = 0;

    while (done < size)
    {
        long long at = offset + (long long)done;
        struct sd_cache_line *line = sd_cache_line_for( file, at );
        size_t skip, count;

        if (!line)
        {
            long long start = at - at % SD_CACHE_CHUNK, got;

            if (sd_cache_known_end( file, start )) break;
            line = sd_cache_victim( file );
            if (!line->data && pool->used >= pool->max)
            {
                /* Full: take the chunk read longest ago, whoever read it. */
                struct sd_cache_line *old = sd_cache_oldest( pool );

                if (!old) return done ? (long long)done : SD_CACHE_BYPASS;
                line->data = old->data;
                old->data = NULL;
                old->start = -1;
                old->len = 0;
                old->lru = 0;
                sd_cache_held_replace( pool, old, line );
            }
            else if (!line->data)
            {
                if (!pool->held && !(pool->held = calloc( pool->max, sizeof(*pool->held) )))
                    return done ? (long long)done : SD_CACHE_BYPASS;
                if (!(line->data = malloc( SD_CACHE_CHUNK )))
                    return done ? (long long)done : SD_CACHE_BYPASS;
                sd_cache_held_add( pool, line );
            }
            line->start = -1;
            line->len = 0;
            (*fills)++;
            if ((got = fill( ctx, start, line->data, SD_CACHE_CHUNK )) < 0)
                return done ? (long long)done : -1;
            line->start = start;
            line->len = (size_t)got;
            if (at >= start + got) break;  /* end of the file */
        }
        line->used_at = ++pool->clock;
        skip = (size_t)(at - line->start);
        count = line->len - skip;
        if (count > size - done) count = size - done;
        memcpy( buf + done, line->data + skip, count );
        done += count;
    }
    return (long long)done;
}

/* FAT ignores ASCII case, repeated and trailing slashes name the same file,
 * and a path without a device is on sdmc, the default device. */
static inline int sd_cache_same_path( const char *a, const char *b )
{
    if (!strncmp( a, "sdmc:", 5 )) a += 5;
    if (!strncmp( b, "sdmc:", 5 )) b += 5;
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

static inline struct sd_cache_file *sd_cache_find( struct sd_cache_file *list, void *key )
{
    for (; list; list = list->next) if (list->key == key) return list;
    return NULL;
}

/* Stop caching every open file with this path. */
static inline void sd_cache_forget_path( struct sd_cache_file *list, struct sd_cache_pool *pool, const char *path )
{
    for (; list; list = list->next)
    {
        if (!sd_cache_same_path( list->path, path )) continue;
        list->cacheable = 0;
        sd_cache_drop( list, pool );
    }
}

/* Record an open file. Returns NULL without memory. */
static inline struct sd_cache_file *sd_cache_opened( struct sd_cache_file **list, struct sd_cache_pool *pool,
                                                     void *key, const char *path, int writable )
{
    struct sd_cache_file *file, *other;

    if (!(file = calloc( 1, sizeof(*file) ))) return NULL;
    if (!(file->path = strdup( path )))
    {
        free( file );
        return NULL;
    }
    file->key = key;
    file->writable = writable;
    file->cacheable = !writable;
    for (other = *list; other; other = other->next)
    {
        if (!sd_cache_same_path( other->path, path )) continue;
        if (other->writable) file->cacheable = 0;
        if (writable)
        {
            other->cacheable = 0;
            sd_cache_drop( other, pool );
        }
    }
    file->next = *list;
    *list = file;
    return file;
}

static inline void sd_cache_closed( struct sd_cache_file **list, struct sd_cache_pool *pool, void *key )
{
    struct sd_cache_file **prev, *file;

    for (prev = list; (file = *prev); prev = &file->next)
    {
        if (file->key != key) continue;
        *prev = file->next;
        sd_cache_drop( file, pool );
        free( file->path );
        free( file );
        return;
    }
}

#endif
