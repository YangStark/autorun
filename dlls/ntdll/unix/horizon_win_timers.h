/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_HORIZON_WIN_TIMERS_H
#define WINE_HORIZON_WIN_TIMERS_H

#include <stdlib.h>

/* SetTimer and SetSystemTimer for the in-process Horizon server, following
 * server/queue.c. A timer belongs to its window's thread, or to the calling
 * thread when it has no window. It becomes a posted WM_TIMER or WM_SYSTIMER
 * message once due; taking that message out of the queue schedules the next
 * period from the current time, so a busy thread gets one message rather
 * than a backlog. Callers hold the server lock and pass a monotonic
 * millisecond clock; nothing here performs I/O, so the host tests include
 * this header directly. */

#define HORIZON_WIN_TIMERS_NO_MEMORY -1
#define HORIZON_WIN_TIMERS_NO_IDS    -2
#define HORIZON_WIN_TIMERS_NOT_FOUND -3

struct horizon_win_timer
{
    unsigned int tid;
    unsigned int win;
    unsigned int msg;
    unsigned int rate;             /* milliseconds */
    unsigned long long id;
    unsigned long long lparam;
    unsigned long long when;       /* next expiration */
    struct horizon_win_timer *next;
};

struct horizon_win_timers
{
    struct horizon_win_timer *head;
    unsigned long long next_id;    /* ids for timers without a window; 0 until first use */
};

static inline struct horizon_win_timer **horizon_win_timers_find( struct horizon_win_timers *timers,
                                                                 unsigned int tid, unsigned int win,
                                                                 unsigned int msg, unsigned long long id )
{
    struct horizon_win_timer **link;

    for (link = &timers->head; *link; link = &(*link)->next)
        if ((*link)->tid == tid && (*link)->win == win && (*link)->msg == msg && (*link)->id == id)
            return link;
    return NULL;
}

static inline void horizon_win_timers_remove( struct horizon_win_timer **link )
{
    struct horizon_win_timer *timer = *link;

    *link = timer->next;
    free( timer );
}

/* Create or replace a timer and return its id in *id_out. A window timer
 * keeps the caller's id. Without a window, an existing id is reused and
 * anything else gets a free id counting down from 0x7fff. */
static inline int horizon_win_timers_set( struct horizon_win_timers *timers, unsigned int tid,
                                          unsigned int win, unsigned int msg, unsigned long long id,
                                          unsigned int rate, unsigned long long lparam,
                                          unsigned long long now, unsigned long long *id_out )
{
    struct horizon_win_timer **link, *timer;

    if (!timers->next_id) timers->next_id = 0x7fff;
    if (win || id)
    {
        if ((link = horizon_win_timers_find( timers, tid, win, msg, id ))) horizon_win_timers_remove( link );
        else if (!win) id = 0;
    }
    if (!win && !id)
    {
        unsigned long long end_id = timers->next_id;

        for (;;)
        {
            id = timers->next_id;
            if (--timers->next_id <= 0x100) timers->next_id = 0x7fff;
            if (!horizon_win_timers_find( timers, tid, 0, msg, id )) break;
            if (timers->next_id == end_id) return HORIZON_WIN_TIMERS_NO_IDS;
        }
    }
    if (!(timer = calloc( 1, sizeof(*timer) ))) return HORIZON_WIN_TIMERS_NO_MEMORY;
    timer->tid = tid;
    timer->win = win;
    timer->msg = msg;
    timer->id = id;
    timer->lparam = lparam;
    timer->rate = rate ? rate : 1;
    timer->when = now + timer->rate;
    timer->next = timers->head;
    timers->head = timer;
    *id_out = id;
    return 0;
}

static inline int horizon_win_timers_kill( struct horizon_win_timers *timers, unsigned int tid,
                                           unsigned int win, unsigned int msg, unsigned long long id )
{
    struct horizon_win_timer **link = horizon_win_timers_find( timers, tid, win, msg, id );

    if (!link) return HORIZON_WIN_TIMERS_NOT_FOUND;
    horizon_win_timers_remove( link );
    return 0;
}

/* The thread's timer that fell due first and passes the filter, or NULL. A
 * window filter matches that window only, as in server/queue.c. With
 * "remove", the timer is rescheduled past "now". */
static inline struct horizon_win_timer *horizon_win_timers_expired( struct horizon_win_timers *timers,
                                                                   unsigned int tid, unsigned int get_win,
                                                                   unsigned int first, unsigned int last,
                                                                   unsigned long long now, int remove )
{
    struct horizon_win_timer *timer, *found = NULL;

    for (timer = timers->head; timer; timer = timer->next)
    {
        if (timer->tid != tid || timer->when > now) continue;
        if (get_win && timer->win != get_win) continue;
        if ((first || last) && (timer->msg < first || timer->msg > last)) continue;
        if (!found || timer->when < found->when) found = timer;
    }
    if (found && remove)
        while (found->when <= now) found->when += found->rate;
    return found;
}

/* Drop a destroyed window's timers, or every timer of an ended thread (win 0). */
static inline void horizon_win_timers_drop( struct horizon_win_timers *timers, unsigned int tid,
                                            unsigned int win )
{
    struct horizon_win_timer **link = &timers->head;

    while (*link)
    {
        if (win ? (*link)->win == win : (*link)->tid == tid) horizon_win_timers_remove( link );
        else link = &(*link)->next;
    }
}

#endif
