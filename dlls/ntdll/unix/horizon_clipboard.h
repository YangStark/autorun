/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_HORIZON_CLIPBOARD_H
#define WINE_HORIZON_CLIPBOARD_H

#include <stdlib.h>
#include <string.h>

/* The clipboard of the in-process Horizon server, following server/clipboard.c.
 * Windows are validated by the caller; a thread is its id. Text, metafile and
 * bitmap formats missing at close are added as synthesized (data NULL, "from"
 * set) and converted by win32u when asked for. Delay-rendered formats (data
 * NULL, "from" 0) are rendered by the owner through WM_RENDERFORMAT. Callers
 * hold the server lock; nothing here performs I/O, so the host tests include
 * this header directly. */

#define HORIZON_CLIP_CF_TEXT         1
#define HORIZON_CLIP_CF_BITMAP       2
#define HORIZON_CLIP_CF_METAFILEPICT 3
#define HORIZON_CLIP_CF_OEMTEXT      7
#define HORIZON_CLIP_CF_DIB          8
#define HORIZON_CLIP_CF_UNICODETEXT  13
#define HORIZON_CLIP_CF_ENHMETAFILE  14
#define HORIZON_CLIP_CF_LOCALE       16
#define HORIZON_CLIP_CF_DIBV5        17
#define HORIZON_CLIP_CF_MAX          18

#define HORIZON_CLIP_STATUS_SUCCESS               0x00000000u
#define HORIZON_CLIP_STATUS_PENDING               0x00000103u
#define HORIZON_CLIP_STATUS_BUFFER_OVERFLOW       0x80000005u
#define HORIZON_CLIP_STATUS_INVALID_PARAMETER     0xc000000du
#define HORIZON_CLIP_STATUS_NO_MEMORY             0xc0000017u
#define HORIZON_CLIP_STATUS_INVALID_LOCK_SEQUENCE 0xc000001eu
#define HORIZON_CLIP_STATUS_BUFFER_TOO_SMALL      0xc0000023u
#define HORIZON_CLIP_STATUS_OBJECT_NAME_NOT_FOUND 0xc0000034u
#define HORIZON_CLIP_STATUS_INVALID_OWNER         0xc000005au
#define HORIZON_CLIP_STATUS_NOT_OPEN              0xc001058au  /* ERROR_CLIPBOARD_NOT_OPEN */

struct horizon_clip_format
{
    unsigned int id;
    unsigned int from;
    unsigned int seqno;
    unsigned int size;
    unsigned char *data;
    struct horizon_clip_format *next;
};

struct horizon_clipboard
{
    unsigned int open_tid;
    unsigned int open_win;
    unsigned int owner;
    unsigned int viewer;
    unsigned int lcid;
    unsigned int seqno;
    unsigned int open_seqno;
    unsigned int rendering;
    struct horizon_clip_format *formats;   /* in the order they were added */
    unsigned int format_count;
    unsigned int format_map;               /* formats below CF_MAX present */
    unsigned int listen_count;
    unsigned int listen_size;
    unsigned int *listeners;
};

static inline struct horizon_clip_format *horizon_clip_get_format( struct horizon_clipboard *clip, unsigned int id )
{
    struct horizon_clip_format *format;

    for (format = clip->formats; format; format = format->next)
        if (format->id == id) return format;
    return NULL;
}

static inline struct horizon_clip_format *horizon_clip_add_format( struct horizon_clipboard *clip, unsigned int id )
{
    struct horizon_clip_format *format, **tail;

    if (!(format = calloc( 1, sizeof(*format) ))) return NULL;
    format->id = id;
    for (tail = &clip->formats; *tail; tail = &(*tail)->next) ;
    *tail = format;
    clip->format_count++;
    if (id < HORIZON_CLIP_CF_MAX) clip->format_map |= 1u << id;
    return format;
}

static inline void horizon_clip_free_formats( struct horizon_clipboard *clip )
{
    struct horizon_clip_format *format;

    while ((format = clip->formats))
    {
        clip->formats = format->next;
        free( format->data );
        free( format );
    }
    clip->format_count = 0;
    clip->format_map = 0;
}

#define HORIZON_CLIP_HAS( map, id ) ((map) & (1u << (id)))

static inline unsigned int horizon_clip_synthesize( struct horizon_clipboard *clip )
{
    static const unsigned int formats[][3] =
    {
        { HORIZON_CLIP_CF_TEXT, HORIZON_CLIP_CF_OEMTEXT, HORIZON_CLIP_CF_UNICODETEXT },
        { HORIZON_CLIP_CF_OEMTEXT, HORIZON_CLIP_CF_UNICODETEXT, HORIZON_CLIP_CF_TEXT },
        { HORIZON_CLIP_CF_UNICODETEXT, HORIZON_CLIP_CF_TEXT, HORIZON_CLIP_CF_OEMTEXT },
        { HORIZON_CLIP_CF_METAFILEPICT, HORIZON_CLIP_CF_ENHMETAFILE, 0 },
        { HORIZON_CLIP_CF_ENHMETAFILE, HORIZON_CLIP_CF_METAFILEPICT, 0 },
        { HORIZON_CLIP_CF_BITMAP, HORIZON_CLIP_CF_DIB, HORIZON_CLIP_CF_DIBV5 },
        { HORIZON_CLIP_CF_DIB, HORIZON_CLIP_CF_BITMAP, HORIZON_CLIP_CF_DIBV5 },
        { HORIZON_CLIP_CF_DIBV5, HORIZON_CLIP_CF_BITMAP, HORIZON_CLIP_CF_DIB },
    };
    unsigned int i, from, total = 0, map = clip->format_map;
    struct horizon_clip_format *format;

    if (!HORIZON_CLIP_HAS( map, HORIZON_CLIP_CF_LOCALE ) &&
        (HORIZON_CLIP_HAS( map, HORIZON_CLIP_CF_TEXT ) || HORIZON_CLIP_HAS( map, HORIZON_CLIP_CF_OEMTEXT ) ||
         HORIZON_CLIP_HAS( map, HORIZON_CLIP_CF_UNICODETEXT )))
    {
        unsigned char *data = malloc( sizeof(clip->lcid) );

        if (data && (format = horizon_clip_add_format( clip, HORIZON_CLIP_CF_LOCALE )))
        {
            memcpy( data, &clip->lcid, sizeof(clip->lcid) );
            format->seqno = clip->seqno++;
            format->data = data;
            format->size = sizeof(clip->lcid);
        }
        else free( data );
    }
    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
    {
        if (HORIZON_CLIP_HAS( map, formats[i][0] )) continue;
        if (HORIZON_CLIP_HAS( map, formats[i][1] )) from = formats[i][1];
        else if (formats[i][2] && HORIZON_CLIP_HAS( map, formats[i][2] )) from = formats[i][2];
        else continue;
        if (!(format = horizon_clip_add_format( clip, formats[i][0] ))) continue;
        format->from = from;
        format->seqno = clip->seqno;
        total++;
    }
    return total;
}

/* Closing after a change synthesizes formats. Returns 1 when the listeners
 * should get WM_CLIPBOARDUPDATE (and *viewer WM_DRAWCLIPBOARD). */
static inline int horizon_clip_close_locked( struct horizon_clipboard *clip, unsigned int *viewer )
{
    *viewer = 0;
    clip->open_win = 0;
    clip->open_tid = 0;
    if (clip->seqno == clip->open_seqno) return 0;
    if (horizon_clip_synthesize( clip )) clip->seqno++;
    *viewer = clip->viewer;
    return 1;
}

/* The owner is gone: delay-rendered formats nobody can render are dropped. */
static inline int horizon_clip_release_locked( struct horizon_clipboard *clip, unsigned int *viewer )
{
    struct horizon_clip_format **link = &clip->formats, *format;
    int changed = 0;

    *viewer = 0;
    clip->owner = 0;
    while ((format = *link))
    {
        if (format->data || (format->from && HORIZON_CLIP_HAS( clip->format_map, format->from )))
        {
            link = &format->next;
            continue;
        }
        *link = format->next;
        if (format->id < HORIZON_CLIP_CF_MAX) clip->format_map &= ~(1u << format->id);
        clip->format_count--;
        free( format );
        changed = 1;
    }
    if (!changed) return 0;
    clip->seqno++;
    *viewer = clip->viewer;
    return 1;
}

static inline unsigned int horizon_clip_open( struct horizon_clipboard *clip, unsigned int tid, unsigned int win,
                                              unsigned int *owner )
{
    if (clip->open_tid && clip->open_win != win) return HORIZON_CLIP_STATUS_INVALID_LOCK_SEQUENCE;
    if (!clip->open_tid) clip->open_seqno = clip->seqno;
    if (clip->open_tid != tid) clip->rendering = 0;
    clip->open_win = win;
    clip->open_tid = tid;
    *owner = clip->owner;
    return HORIZON_CLIP_STATUS_SUCCESS;
}

static inline unsigned int horizon_clip_empty( struct horizon_clipboard *clip, unsigned int tid )
{
    if (clip->open_tid != tid) return HORIZON_CLIP_STATUS_NOT_OPEN;
    horizon_clip_free_formats( clip );
    clip->owner = clip->open_win;
    clip->seqno++;
    return HORIZON_CLIP_STATUS_SUCCESS;
}

/* Data NULL with size 0 marks a delay-rendered format. */
static inline unsigned int horizon_clip_set_data( struct horizon_clipboard *clip, unsigned int tid,
                                                  unsigned int id, unsigned int lcid, const void *data,
                                                  unsigned int size, unsigned int *seqno )
{
    struct horizon_clip_format *format;
    unsigned char *copy = NULL;

    (void)tid;
    if (!id || !clip->open_tid) return HORIZON_CLIP_STATUS_NOT_OPEN;
    if (size)
    {
        if (!(copy = malloc( size ))) return HORIZON_CLIP_STATUS_NO_MEMORY;
        memcpy( copy, data, size );
    }
    if (!(format = horizon_clip_get_format( clip, id )) && !(format = horizon_clip_add_format( clip, id )))
    {
        free( copy );
        return HORIZON_CLIP_STATUS_NO_MEMORY;
    }
    free( format->data );
    format->from = 0;
    format->seqno = clip->seqno;
    format->size = size;
    format->data = copy;
    if (!clip->rendering) clip->seqno++;
    if (id == HORIZON_CLIP_CF_TEXT || id == HORIZON_CLIP_CF_OEMTEXT || id == HORIZON_CLIP_CF_UNICODETEXT)
        clip->lcid = lcid;
    *seqno = format->seqno;
    return HORIZON_CLIP_STATUS_SUCCESS;
}

struct horizon_clip_data_reply
{
    unsigned int from;
    unsigned int owner;
    unsigned int seqno;
    unsigned int total;
    const unsigned char *data;  /* to return, or NULL */
};

static inline unsigned int horizon_clip_get_data( struct horizon_clipboard *clip, unsigned int tid, unsigned int id,
                                                  int render, int cached, unsigned int seqno,
                                                  unsigned int reply_max, struct horizon_clip_data_reply *reply )
{
    struct horizon_clip_format *format;
    unsigned int status = HORIZON_CLIP_STATUS_SUCCESS;

    memset( reply, 0, sizeof(*reply) );
    if (clip->open_tid != tid) return HORIZON_CLIP_STATUS_NOT_OPEN;
    if (!(format = horizon_clip_get_format( clip, id )))
    {
        status = HORIZON_CLIP_STATUS_OBJECT_NAME_NOT_FOUND;
        goto done;
    }
    reply->from = format->from;
    reply->total = format->size;
    reply->seqno = format->seqno;
    reply->owner = clip->owner;
    if (!format->data && render)  /* the client renders it */
    {
        if (format->from || clip->owner) clip->rendering++;
        return status;
    }
    if (cached && seqno == format->seqno) goto done;  /* the client's copy is current */
    if (format->data)
    {
        if (format->size > reply_max) return HORIZON_CLIP_STATUS_BUFFER_OVERFLOW;
        reply->data = format->data;
    }
done:
    if (!render && clip->rendering) clip->rendering--;
    return status;
}

/* With "id" 0 fills up to "max" formats and returns BUFFER_TOO_SMALL if they
 * do not fit; otherwise reports whether "id" is present. */
static inline unsigned int horizon_clip_formats( struct horizon_clipboard *clip, unsigned int id,
                                                 unsigned int *list, unsigned int max, unsigned int *count )
{
    struct horizon_clip_format *format;
    unsigned int i = 0;

    if (id)
    {
        *count = horizon_clip_get_format( clip, id ) != NULL;
        return HORIZON_CLIP_STATUS_SUCCESS;
    }
    *count = clip->format_count;
    if (clip->format_count > max) return HORIZON_CLIP_STATUS_BUFFER_TOO_SMALL;
    for (format = clip->formats; format; format = format->next) list[i++] = format->id;
    return HORIZON_CLIP_STATUS_SUCCESS;
}

static inline unsigned int horizon_clip_enum( struct horizon_clipboard *clip, unsigned int tid,
                                              unsigned int previous, unsigned int *next )
{
    struct horizon_clip_format *format = clip->formats;

    *next = 0;
    if (clip->open_tid != tid) return HORIZON_CLIP_STATUS_NOT_OPEN;
    if (previous)
    {
        while (format && format->id != previous) format = format->next;
        if (format) format = format->next;
    }
    if (format) *next = format->id;
    return HORIZON_CLIP_STATUS_SUCCESS;
}

static inline unsigned int horizon_clip_add_listener( struct horizon_clipboard *clip, unsigned int win )
{
    unsigned int i;

    for (i = 0; i < clip->listen_count; i++)
        if (clip->listeners[i] == win) return HORIZON_CLIP_STATUS_INVALID_PARAMETER;
    if (clip->listen_count == clip->listen_size)
    {
        unsigned int size = clip->listen_size ? clip->listen_size * 2 : 8;
        unsigned int *list = realloc( clip->listeners, size * sizeof(*list) );

        if (!list) return HORIZON_CLIP_STATUS_NO_MEMORY;
        clip->listeners = list;
        clip->listen_size = size;
    }
    clip->listeners[clip->listen_count++] = win;
    return HORIZON_CLIP_STATUS_SUCCESS;
}

static inline int horizon_clip_remove_listener( struct horizon_clipboard *clip, unsigned int win )
{
    unsigned int i;

    for (i = 0; i < clip->listen_count; i++)
    {
        if (clip->listeners[i] != win) continue;
        memmove( clip->listeners + i, clip->listeners + i + 1, (clip->listen_count - i - 1) * sizeof(*clip->listeners) );
        clip->listen_count--;
        return 1;
    }
    return 0;
}

/* set_clipboard_viewer: PENDING asks win32u to send the change through the chain. */
static inline unsigned int horizon_clip_set_viewer( struct horizon_clipboard *clip, unsigned int viewer,
                                                    unsigned int previous, unsigned int *old_viewer,
                                                    unsigned int *owner )
{
    *old_viewer = clip->viewer;
    *owner = clip->owner;
    if (previous && clip->viewer != previous) return HORIZON_CLIP_STATUS_PENDING;
    clip->viewer = viewer;
    return HORIZON_CLIP_STATUS_SUCCESS;
}

/* A destroyed window stops listening, viewing, owning or holding the clipboard
 * open. Returns 1 when listeners should be told about a change. */
static inline int horizon_clip_window_destroyed( struct horizon_clipboard *clip, unsigned int win )
{
    unsigned int viewer;
    int notify = 0;

    horizon_clip_remove_listener( clip, win );
    if (clip->viewer == win) clip->viewer = 0;
    if (clip->owner == win) notify |= horizon_clip_release_locked( clip, &viewer );
    if (clip->open_win == win && clip->open_tid) notify |= horizon_clip_close_locked( clip, &viewer );
    return notify;
}

/* A thread that ends with the clipboard open closes it. */
static inline int horizon_clip_thread_ended( struct horizon_clipboard *clip, unsigned int tid )
{
    unsigned int viewer;

    if (!tid || clip->open_tid != tid) return 0;
    return horizon_clip_close_locked( clip, &viewer );
}

#endif
