/* Host test for the Horizon server's window timers (dlls/ntdll/unix/horizon_win_timers.h). */
#include <assert.h>
#include <stdio.h>

#include "../../dlls/ntdll/unix/horizon_win_timers.h"

#define WM_TIMER 0x0113
#define WM_SYSTIMER 0x0118
#define SYSTEM_TIMER_CARET 0xffff
#define MAIN 0x10038
#define EDIT 0x10042

static unsigned long long set( struct horizon_win_timers *timers, unsigned int tid, unsigned int win,
                               unsigned int msg, unsigned long long id, unsigned int rate,
                               unsigned long long now )
{
    unsigned long long out = 0xdead;

    assert( !horizon_win_timers_set( timers, tid, win, msg, id, rate, 0x1234, now, &out ) );
    return out;
}

static unsigned int count( struct horizon_win_timers *timers )
{
    struct horizon_win_timer *timer;
    unsigned int n = 0;

    for (timer = timers->head; timer; timer = timer->next) n++;
    return n;
}

static void test_caret_blink(void)
{
    struct horizon_win_timers timers = {0};
    struct horizon_win_timer *timer;
    unsigned long long now = 1000;
    unsigned int blinks = 0;

    /* NtUserShowCaret: SetSystemTimer( edit, SYSTEM_TIMER_CARET, 500 ). */
    assert( set( &timers, 4, EDIT, WM_SYSTIMER, SYSTEM_TIMER_CARET, 500, now ) == SYSTEM_TIMER_CARET );
    assert( !horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, 1499, 1 ) );
    /* GetMessage polling every 10 ms for two seconds. */
    for (now = 1000; now <= 3000; now += 10)
    {
        if (!(timer = horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, now, 1 ))) continue;
        assert( timer->win == EDIT && timer->msg == WM_SYSTIMER && timer->id == SYSTEM_TIMER_CARET );
        assert( !timer->lparam || timer->lparam == 0x1234 );
        blinks++;
    }
    assert( blinks == 4 );  /* at 1500, 2000, 2500 and 3000 */
    /* Another thread never sees it. */
    assert( !horizon_win_timers_expired( &timers, 5, 0, 0, ~0u, 9999, 0 ) );
    horizon_win_timers_drop( &timers, 4, EDIT );
    assert( !timers.head );
}

static void test_peek_and_coalesce(void)
{
    struct horizon_win_timers timers = {0};
    struct horizon_win_timer *timer;

    set( &timers, 4, MAIN, WM_TIMER, 7, 20, 0 );
    /* PM_NOREMOVE leaves it due... */
    timer = horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, 25, 0 );
    assert( timer && timer->id == 7 && timer->when == 20 );
    assert( horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, 25, 0 ) == timer );
    /* ...a thread that did not pump for 200 ms gets one message, not ten. */
    assert( horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, 205, 1 ) == timer );
    assert( timer->when == 220 );
    assert( !horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, 219, 1 ) );
    assert( horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, 220, 1 ) == timer && timer->when == 240 );
    horizon_win_timers_drop( &timers, 4, 0 );
    assert( !timers.head );
}

static void test_filters_and_order(void)
{
    struct horizon_win_timers timers = {0};
    struct horizon_win_timer *timer;

    set( &timers, 4, MAIN, WM_TIMER, 1, 50, 0 );
    set( &timers, 4, EDIT, WM_SYSTIMER, SYSTEM_TIMER_CARET, 30, 0 );
    set( &timers, 4, 0, WM_TIMER, 0, 40, 0 );
    /* The one due first comes first. */
    timer = horizon_win_timers_expired( &timers, 4, 0, 0, ~0u, 60, 0 );
    assert( timer->win == EDIT );
    /* A window filter is that window only, not its children. */
    timer = horizon_win_timers_expired( &timers, 4, MAIN, 0, ~0u, 60, 0 );
    assert( timer->win == MAIN && timer->id == 1 );
    assert( !horizon_win_timers_expired( &timers, 4, 0xffffffff, 0, ~0u, 60, 0 ) );
    /* Message ranges. */
    timer = horizon_win_timers_expired( &timers, 4, 0, WM_TIMER, WM_TIMER, 60, 1 );
    assert( timer->win == 0 && timer->when == 80 );
    timer = horizon_win_timers_expired( &timers, 4, 0, WM_TIMER, WM_TIMER, 60, 1 );
    assert( timer->win == MAIN && timer->when == 100 );
    assert( !horizon_win_timers_expired( &timers, 4, 0, WM_TIMER, WM_TIMER, 60, 0 ) );
    assert( horizon_win_timers_expired( &timers, 4, 0, WM_SYSTIMER, WM_SYSTIMER, 60, 0 )->win == EDIT );
    horizon_win_timers_drop( &timers, 4, 0 );
}

static void test_ids_replace_and_kill(void)
{
    struct horizon_win_timers timers = {0};
    unsigned long long a, b, c;

    /* Window timers keep their id, including 0; setting again replaces. */
    assert( set( &timers, 4, MAIN, WM_TIMER, 0, 10, 0 ) == 0 );
    assert( set( &timers, 4, MAIN, WM_TIMER, 7, 10, 0 ) == 7 );
    assert( set( &timers, 4, MAIN, WM_TIMER, 7, 99, 50 ) == 7 );
    assert( count( &timers ) == 2 );
    assert( horizon_win_timers_find( &timers, 4, MAIN, WM_TIMER, 7 )[0]->when == 149 );
    /* The same id as a system timer is a different timer. */
    assert( set( &timers, 4, MAIN, WM_SYSTIMER, 7, 10, 0 ) == 7 && count( &timers ) == 3 );

    /* Thread timers get ids from 0x7fff down; a known id is reused, others are not honored. */
    a = set( &timers, 4, 0, WM_TIMER, 0, 10, 0 );
    b = set( &timers, 4, 0, WM_TIMER, 1234, 10, 0 );
    assert( a == 0x7fff && b == 0x7ffe );
    assert( set( &timers, 4, 0, WM_TIMER, a, 30, 0 ) == a && count( &timers ) == 5 );
    c = set( &timers, 5, 0, WM_TIMER, 0, 10, 0 );
    assert( c == 0x7ffd );

    assert( !horizon_win_timers_kill( &timers, 4, 0, WM_TIMER, b ) );
    assert( horizon_win_timers_kill( &timers, 4, 0, WM_TIMER, b ) == HORIZON_WIN_TIMERS_NOT_FOUND );
    assert( horizon_win_timers_kill( &timers, 5, 0, WM_TIMER, a ) == HORIZON_WIN_TIMERS_NOT_FOUND );
    assert( horizon_win_timers_kill( &timers, 4, MAIN, WM_SYSTIMER, 0 ) == HORIZON_WIN_TIMERS_NOT_FOUND );
    assert( !horizon_win_timers_kill( &timers, 4, MAIN, WM_TIMER, 0 ) );

    /* Destroying MAIN drops its timers; ending thread 4 drops the rest of its own. */
    horizon_win_timers_drop( &timers, 4, MAIN );
    assert( count( &timers ) == 2 );
    horizon_win_timers_drop( &timers, 4, 0 );
    assert( count( &timers ) == 1 && timers.head->tid == 5 );
    horizon_win_timers_drop( &timers, 5, 0 );
    assert( !timers.head );
}

static void test_id_wrap(void)
{
    struct horizon_win_timers timers = {0};
    unsigned long long id, first = 0;
    unsigned int i;

    /* Ids stay in 0x101..0x7fff and skip ones still in use. */
    for (i = 0; i < 0x7eff; i++)
    {
        id = set( &timers, 4, 0, WM_TIMER, 0, 10, 0 );
        assert( id > 0x100 && id <= 0x7fff );
        if (!i) first = id;
    }
    assert( first == 0x7fff && id == 0x101 );
    assert( !horizon_win_timers_kill( &timers, 4, 0, WM_TIMER, 0x4000 ) );
    assert( set( &timers, 4, 0, WM_TIMER, 0, 10, 0 ) == 0x4000 );
    id = 0xdead;
    assert( horizon_win_timers_set( &timers, 4, 0, WM_TIMER, 0, 10, 0, 0, &id ) == HORIZON_WIN_TIMERS_NO_IDS );
    assert( id == 0xdead );
    horizon_win_timers_drop( &timers, 4, 0 );
    assert( !timers.head );
}

int main(void)
{
    test_caret_blink();
    test_peek_and_coalesce();
    test_filters_and_order();
    test_ids_replace_and_kill();
    test_id_wrap();
    puts( "Horizon window timers: caret blink, peek, coalescing, filters, ids, replace, kill and cleanup passed" );
    return 0;
}
