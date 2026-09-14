/* Host test for the Horizon server's I/O completion ports (dlls/ntdll/unix/horizon_completion.h). */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "wine/server_protocol.h"

struct horizon_server_request_header { int req; unsigned int request_size, reply_size; };
struct horizon_server_reply_header { unsigned int error, reply_size; };
#include "../../dlls/ntdll/unix/horizon_completion.h"

#define CHECK_LAYOUT(n) _Static_assert( sizeof(struct horizon_##n) == sizeof(struct n), #n )
#define CHECK_FIELD(n, f) _Static_assert( offsetof(struct horizon_##n, f) == offsetof(struct n, f), #n "." #f )
#define CHECK_REQ(r, n) _Static_assert( HORIZON_REQ_##r == REQ_##n, #n )
CHECK_LAYOUT(create_completion_request);
CHECK_LAYOUT(create_completion_reply);
CHECK_LAYOUT(add_completion_request);
CHECK_LAYOUT(remove_completion_request);
CHECK_LAYOUT(remove_completion_reply);
CHECK_LAYOUT(get_thread_completion_reply);
CHECK_LAYOUT(query_completion_request);
CHECK_LAYOUT(query_completion_reply);
CHECK_LAYOUT(set_completion_info_request);
CHECK_LAYOUT(add_fd_completion_request);
CHECK_LAYOUT(set_fd_completion_mode_request);
CHECK_FIELD(create_completion_request, concurrent);
CHECK_FIELD(create_completion_reply, handle);
CHECK_FIELD(add_completion_request, handle);
CHECK_FIELD(add_completion_request, ckey);
CHECK_FIELD(add_completion_request, cvalue);
CHECK_FIELD(add_completion_request, information);
CHECK_FIELD(add_completion_request, reserve_handle);
CHECK_FIELD(add_completion_request, status);
CHECK_FIELD(remove_completion_request, handle);
CHECK_FIELD(remove_completion_request, alertable);
CHECK_FIELD(remove_completion_reply, ckey);
CHECK_FIELD(remove_completion_reply, cvalue);
CHECK_FIELD(remove_completion_reply, information);
CHECK_FIELD(remove_completion_reply, status);
CHECK_FIELD(remove_completion_reply, wait_handle);
CHECK_FIELD(get_thread_completion_reply, ckey);
CHECK_FIELD(get_thread_completion_reply, cvalue);
CHECK_FIELD(get_thread_completion_reply, information);
CHECK_FIELD(get_thread_completion_reply, status);
CHECK_FIELD(query_completion_request, handle);
CHECK_FIELD(query_completion_reply, depth);
CHECK_FIELD(set_completion_info_request, handle);
CHECK_FIELD(set_completion_info_request, ckey);
CHECK_FIELD(set_completion_info_request, chandle);
CHECK_FIELD(add_fd_completion_request, handle);
CHECK_FIELD(add_fd_completion_request, cvalue);
CHECK_FIELD(add_fd_completion_request, information);
CHECK_FIELD(add_fd_completion_request, status);
CHECK_FIELD(add_fd_completion_request, async);
CHECK_FIELD(set_fd_completion_mode_request, handle);
CHECK_FIELD(set_fd_completion_mode_request, flags);
CHECK_REQ(CREATE_COMPLETION, create_completion);
CHECK_REQ(OPEN_COMPLETION, open_completion);
CHECK_REQ(ADD_COMPLETION, add_completion);
CHECK_REQ(REMOVE_COMPLETION, remove_completion);
CHECK_REQ(GET_THREAD_COMPLETION, get_thread_completion);
CHECK_REQ(QUERY_COMPLETION, query_completion);
CHECK_REQ(SET_COMPLETION_INFO, set_completion_info);
CHECK_REQ(ADD_FD_COMPLETION, add_fd_completion);
CHECK_REQ(SET_FD_COMPLETION_MODE, set_fd_completion_mode);

/* Messages leave in the order they came, through emptying and refilling. */
static void test_queue(void)
{
    struct horizon_completion_queue queue = {0};
    struct horizon_completion_msg msg;
    unsigned int i;

    assert( !horizon_completion_take( &queue, &msg ) && !queue.depth );
    for (i = 0; i < 3; i++) assert( horizon_completion_add( &queue, 10 + i, 0xfbef0000u + i, i, 100 * i ) );
    assert( queue.depth == 3 );
    for (i = 0; i < 3; i++)
    {
        assert( horizon_completion_take( &queue, &msg ) );
        assert( msg.ckey == 10 + i && msg.cvalue == 0xfbef0000u + i && msg.status == i &&
                msg.information == 100 * i && !msg.next && queue.depth == 2 - i );
    }
    assert( !queue.head && !queue.tail && !horizon_completion_take( &queue, &msg ) );

    assert( horizon_completion_add( &queue, 1, 1, 0, 0 ) && horizon_completion_add( &queue, 2, 2, 0, 0 ) );
    assert( horizon_completion_take( &queue, &msg ) && msg.ckey == 1 );
    assert( horizon_completion_add( &queue, 3, 3, 0, 0 ) );
    assert( horizon_completion_take( &queue, &msg ) && msg.ckey == 2 );
    assert( horizon_completion_take( &queue, &msg ) && msg.ckey == 3 && !queue.depth && !queue.tail );

    assert( horizon_completion_add( &queue, 4, 4, 0, 0 ) && horizon_completion_add( &queue, 5, 5, 0, 0 ) );
    horizon_completion_clear( &queue );
    assert( !queue.head && !queue.tail && !queue.depth );
}

/* NtRemoveIoCompletion's wait: not signaled on an empty port, takes a message
 * once one is posted, abandoned with no port or once the port is closed. */
static void test_wait(void)
{
    struct horizon_completion_queue port = {0};
    struct horizon_completion_msg msg = {0};
    int has_msg = 0;

    assert( horizon_completion_wait_signaled( NULL, 0 ) );
    assert( horizon_completion_wait_satisfy( NULL, 0, &msg, &has_msg ) == 1 && !has_msg );
    assert( !horizon_completion_wait_signaled( &port, 0 ) );

    assert( horizon_completion_add( &port, 0, 0x0058f000, 0, 4096 ) );
    assert( horizon_completion_wait_signaled( &port, 0 ) );
    assert( !horizon_completion_wait_satisfy( &port, 0, &msg, &has_msg ) );
    assert( has_msg && msg.cvalue == 0x0058f000 && msg.information == 4096 && !port.depth );
    assert( !horizon_completion_wait_signaled( &port, 0 ) );

    has_msg = 0;
    assert( horizon_completion_add( &port, 0, 1, 0, 0 ) );
    assert( horizon_completion_wait_signaled( &port, 1 ) );
    assert( horizon_completion_wait_satisfy( &port, 1, &msg, &has_msg ) == 1 && !has_msg && port.depth == 1 );
    horizon_completion_clear( &port );
}

/* Which file handles take a port, which I/O results reach it, and the mode flags. */
static void test_files(void)
{
    assert( horizon_completion_file_overlapped( 0x40 ) );  /* FILE_NON_DIRECTORY_FILE, as quartz opens a movie */
    assert( !horizon_completion_file_overlapped( 0x60 ) && !horizon_completion_file_overlapped( 0x50 ) );

    assert( horizon_completion_file_posts( 0, 0 ) && horizon_completion_file_posts( 1, 0 ) );
    assert( horizon_completion_file_posts( 1, HORIZON_FILE_SKIP_COMPLETION_PORT_ON_SUCCESS ) );
    assert( !horizon_completion_file_posts( 0, HORIZON_FILE_SKIP_COMPLETION_PORT_ON_SUCCESS ) );

    assert( horizon_completion_file_mode( 0, 0xffffffff ) == 7 );
    assert( horizon_completion_file_mode( HORIZON_FILE_SKIP_COMPLETION_PORT_ON_SUCCESS, 0 ) ==
            HORIZON_FILE_SKIP_COMPLETION_PORT_ON_SUCCESS );
}

/* quartz's async reader (dlls/quartz/filesource.c): each overlapped ReadFile on
 * the movie reports STATUS_PENDING with its result posted (async), so the I/O
 * thread gets its OVERLAPPED back as the completion value; a posted key of 1
 * ends the thread, and closing the port abandons a thread still waiting. */
static void test_quartz_reader(void)
{
    struct horizon_completion_queue port = {0};
    struct horizon_completion_msg msg = {0};
    unsigned int flags = horizon_completion_file_mode( 0, 0 );
    int has_msg = 0, i;

    assert( horizon_completion_file_overlapped( 0x40 ) );
    for (i = 0; i < 4; i++)
        if (horizon_completion_file_posts( 1, flags ))
            assert( horizon_completion_add( &port, 0, 0x00b52300 + 0x18 * i, 0, 0x8000 ) );
    assert( horizon_completion_add( &port, 1, 0, 0, 0 ) );
    for (i = 0; i < 4; i++)
    {
        assert( horizon_completion_wait_signaled( &port, 0 ) );
        assert( !horizon_completion_wait_satisfy( &port, 0, &msg, &has_msg ) && has_msg );
        assert( !msg.ckey && msg.cvalue == 0x00b52300u + 0x18 * i && msg.information == 0x8000 );
    }
    assert( !horizon_completion_wait_satisfy( &port, 0, &msg, &has_msg ) && msg.ckey == 1 && !port.depth );
    assert( !horizon_completion_wait_signaled( &port, 0 ) );
    assert( horizon_completion_wait_signaled( &port, 1 ) );
}

int main(void)
{
    test_queue();
    test_wait();
    test_files();
    test_quartz_reader();
    puts( "Horizon completion ports: wire layouts, request numbers, queue order, waits, abandonment, "
          "file association rules and quartz's async reader passed" );
    return 0;
}
