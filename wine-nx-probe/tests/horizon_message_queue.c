/* Host test for the Horizon server's posted message queue (dlls/ntdll/unix/horizon_message_queue.h). */
#include <assert.h>
#include <stdio.h>

#include "../../dlls/ntdll/unix/horizon_message_queue.h"

#define WM_NULL 0x0000
#define WM_COMMAND 0x0111
#define WM_TIMER 0x0113
#define WM_USER 0x0400
#define MAIN 0x10038
#define EDIT 0x10042
#define DIALOG 0x10070

/* EDIT is a child of MAIN; DIALOG is a separate top-level window. */
static int is_descendant( void *ctx, unsigned int child, unsigned int ancestor )
{
    (void)ctx;
    return child == ancestor || (child == EDIT && ancestor == MAIN);
}

static void post( struct horizon_message_queue *queue, unsigned int tid, unsigned int win, unsigned int msg,
                  unsigned long long wparam )
{
    struct horizon_posted_message message = { .tid = tid, .win = win, .msg = msg, .wparam = wparam };

    assert( !horizon_message_queue_post( queue, &message ) );
}

static struct horizon_posted_message **find( struct horizon_message_queue *queue, unsigned int tid,
                                             unsigned int win, unsigned int first, unsigned int last )
{
    return horizon_message_queue_find( queue, tid, win, first, last, is_descendant, NULL );
}

static unsigned int count( struct horizon_message_queue *queue )
{
    struct horizon_posted_message *message;
    unsigned int n = 0;

    for (message = queue->head; message; message = message->next) n++;
    return n;
}

static void test_menu_command_reaches_window(void)
{
    struct horizon_message_queue queue;
    struct horizon_posted_message **link;

    horizon_message_queue_init( &queue );
    assert( !find( &queue, 7, 0, 0, ~0u ) );
    /* NtUserPostMessage( notepad, WM_COMMAND, CMD_FONT, 0 ) from the menu loop. */
    post( &queue, 7, MAIN, WM_COMMAND, 0x1f5 );
    /* PeekMessage( PM_NOREMOVE ) sees it and leaves it queued... */
    link = find( &queue, 7, 0, 0, ~0u );
    assert( link && (*link)->win == MAIN && (*link)->msg == WM_COMMAND && (*link)->wparam == 0x1f5 );
    assert( find( &queue, 7, 0, 0, ~0u ) == link );
    /* ...and GetMessage removes it. */
    horizon_message_queue_remove( &queue, link );
    assert( !find( &queue, 7, 0, 0, ~0u ) && !queue.head && queue.tail == &queue.head );
}

static void test_fifo_threads_and_ranges(void)
{
    struct horizon_message_queue queue;
    struct horizon_posted_message **link;

    horizon_message_queue_init( &queue );
    post( &queue, 7, MAIN, WM_USER, 1 );
    post( &queue, 9, DIALOG, WM_COMMAND, 2 );  /* another thread's */
    post( &queue, 7, MAIN, WM_COMMAND, 3 );
    post( &queue, 7, EDIT, WM_USER, 4 );

    link = find( &queue, 7, 0, 0, ~0u );
    assert( (*link)->wparam == 1 );
    /* A range skips earlier messages without reordering them. */
    link = find( &queue, 7, 0, WM_COMMAND, WM_COMMAND );
    assert( (*link)->wparam == 3 );
    horizon_message_queue_remove( &queue, link );
    assert( !find( &queue, 7, 0, WM_COMMAND, WM_COMMAND ) );
    link = find( &queue, 9, 0, WM_COMMAND, WM_COMMAND );
    assert( link && (*link)->wparam == 2 );
    assert( (*find( &queue, 7, 0, 0, ~0u ))->wparam == 1 );
    assert( !find( &queue, 7, 0, WM_TIMER, WM_TIMER ) );

    /* Removing the last message keeps the tail valid for the next post. */
    link = find( &queue, 7, 0, WM_USER, WM_USER );
    horizon_message_queue_remove( &queue, link );
    link = find( &queue, 7, 0, WM_USER, WM_USER );
    assert( (*link)->wparam == 4 && !(*link)->next );
    horizon_message_queue_remove( &queue, link );
    post( &queue, 7, MAIN, WM_NULL, 5 );
    assert( count( &queue ) == 2 && (*find( &queue, 7, 0, 0, ~0u ))->wparam == 5 );
    horizon_message_queue_drop( &queue, 7, 0 );
    horizon_message_queue_drop( &queue, 9, 0 );
    assert( !queue.head && queue.tail == &queue.head );
}

static void test_window_filters(void)
{
    struct horizon_message_queue queue;
    struct horizon_posted_message **link;

    horizon_message_queue_init( &queue );
    post( &queue, 7, 0, WM_USER, 1 );       /* PostThreadMessage */
    post( &queue, 7, DIALOG, WM_NULL, 2 );  /* EndDialog's wake-up */
    post( &queue, 7, EDIT, WM_USER, 3 );

    /* A window filter includes its children but not thread messages. */
    link = find( &queue, 7, MAIN, 0, ~0u );
    assert( link && (*link)->wparam == 3 );
    link = find( &queue, 7, DIALOG, 0, ~0u );
    assert( link && (*link)->wparam == 2 );
    assert( !find( &queue, 7, EDIT, WM_NULL, WM_NULL + 1 ) );
    /* HWND -1 (and 1) ask for messages posted without a window. */
    link = find( &queue, 7, HORIZON_MESSAGE_QUEUE_THREAD_ONLY, 0, ~0u );
    assert( link && (*link)->wparam == 1 );
    link = find( &queue, 7, 1, 0, ~0u );
    assert( link && (*link)->wparam == 1 );

    /* Destroying a window drops what was posted to it only. */
    horizon_message_queue_drop( &queue, 7, DIALOG );
    assert( count( &queue ) == 2 && !find( &queue, 7, DIALOG, 0, ~0u ) );
    horizon_message_queue_drop( &queue, 7, EDIT );
    assert( count( &queue ) == 1 && (*find( &queue, 7, 0, 0, ~0u ))->wparam == 1 );
    post( &queue, 7, MAIN, WM_USER, 4 );
    assert( count( &queue ) == 2 );
    horizon_message_queue_drop( &queue, 7, 0 );
    assert( !queue.head && queue.tail == &queue.head );
}

int main(void)
{
    test_menu_command_reaches_window();
    test_fifo_threads_and_ranges();
    test_window_filters();
    puts( "Horizon message queue: menu command, FIFO, threads, ranges, window filters and cleanup passed" );
    return 0;
}
