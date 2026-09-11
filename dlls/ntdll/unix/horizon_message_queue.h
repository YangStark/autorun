/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_HORIZON_MESSAGE_QUEUE_H
#define WINE_HORIZON_MESSAGE_QUEUE_H

#include <stdlib.h>

/* Posted messages for the in-process Horizon server. PostMessage,
 * PostThreadMessage and PostQuitMessage always go through the server, even
 * when a thread posts to itself: a chosen menu item reaches its window as a
 * posted WM_COMMAND, and EndDialog wakes the modal loop with a posted WM_NULL.
 * Matching follows server/queue.c (match_window, check_msg_filter). Callers
 * hold the server lock; nothing here performs I/O, so the host tests include
 * this header directly. */

#define HORIZON_MESSAGE_QUEUE_WM_QUIT 0x0012u
#define HORIZON_MESSAGE_QUEUE_THREAD_ONLY 0xffffffffu /* HWND -1: messages without a window */

struct horizon_posted_message
{
    unsigned int tid;
    unsigned int win;
    unsigned int msg;
    unsigned long long wparam;
    unsigned long long lparam;
    int x;
    int y;
    unsigned int time;
    struct horizon_posted_message *next;
};

struct horizon_message_queue
{
    struct horizon_posted_message *head;
    struct horizon_posted_message **tail;
};

/* Whether window "child" is "ancestor" or one of its descendants. */
typedef int (*horizon_message_queue_descendant_fn)( void *ctx, unsigned int child, unsigned int ancestor );

static inline void horizon_message_queue_init( struct horizon_message_queue *queue )
{
    queue->head = NULL;
    queue->tail = &queue->head;
}

/* Append a copy of "message". Returns 0, or -1 when out of memory. */
static inline int horizon_message_queue_post( struct horizon_message_queue *queue,
                                              const struct horizon_posted_message *message )
{
    struct horizon_posted_message *copy = malloc( sizeof(*copy) );

    if (!copy) return -1;
    *copy = *message;
    copy->next = NULL;
    *queue->tail = copy;
    queue->tail = &copy->next;
    return 0;
}

static inline int horizon_message_queue_matches( const struct horizon_posted_message *message,
                                                 unsigned int get_win, unsigned int first, unsigned int last,
                                                 horizon_message_queue_descendant_fn is_descendant, void *ctx )
{
    if ((first || last) && (message->msg < first || message->msg > last)) return 0;
    if (!get_win) return 1;
    if (get_win == HORIZON_MESSAGE_QUEUE_THREAD_ONLY || get_win == 1) return !message->win;
    if (!message->win) return 0;
    return message->win == get_win || is_descendant( ctx, message->win, get_win );
}

/* The link to the oldest message of thread "tid" passing the filter, or NULL. */
static inline struct horizon_posted_message **horizon_message_queue_find(
    struct horizon_message_queue *queue, unsigned int tid, unsigned int get_win, unsigned int first,
    unsigned int last, horizon_message_queue_descendant_fn is_descendant, void *ctx )
{
    struct horizon_posted_message **link;

    for (link = &queue->head; *link; link = &(*link)->next)
    {
        if ((*link)->tid != tid) continue;
        if (horizon_message_queue_matches( *link, get_win, first, last, is_descendant, ctx )) return link;
    }
    return NULL;
}

static inline void horizon_message_queue_remove( struct horizon_message_queue *queue,
                                                 struct horizon_posted_message **link )
{
    struct horizon_posted_message *message = *link;

    *link = message->next;
    if (queue->tail == &message->next) queue->tail = link;
    free( message );
}

/* Drop what was posted to a destroyed window, or to any window of an ended
 * thread (win 0 matches by thread instead). */
static inline void horizon_message_queue_drop( struct horizon_message_queue *queue, unsigned int tid,
                                               unsigned int win )
{
    struct horizon_posted_message **link = &queue->head;

    while (*link)
    {
        if (win ? (*link)->win == win : (*link)->tid == tid) horizon_message_queue_remove( queue, link );
        else link = &(*link)->next;
    }
}

#endif
