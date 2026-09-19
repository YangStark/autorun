#!/usr/bin/env python3
"""Operations on a socket that wait (dlls/ntdll/unix/horizon_async.h): the
bookkeeping behind overlapped Winsock on the in-process server.

The completion rule is server/async.c's async_set_result, and a program built on
Asio -- EA's launcher emulation among them -- hangs for good on any case where
it differs: a packet it waits for that never comes, or one it did not expect."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = (root / 'dlls/ntdll/unix/horizon_async.h').read_text()
wine_async = (root / 'server/async.c').read_text()

# The rule this mirrors, as Wine writes it.
rule = wine_async[wine_async.index('void async_set_result'):]
rule = rule[:rule.index('if (!async->signaled)')]
assert 'if (async->pending || !NT_ERROR( status ))' in rule
assert 'if (async->data.apc)' in rule
assert '!(async->comp_flags & FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)' in rule
assert 'if (async->event) set_event( async->event );' in rule

fixture = r'''
#include <assert.h>
#include <stdio.h>

@HEADER@

static struct horizon_async *make( unsigned int sock, int direction, int state )
{
    struct horizon_async *async = calloc( 1, sizeof(*async) );

    async->sock = sock;
    async->direction = direction;
    async->state = state;
    return async;
}

int main( void )
{
    struct horizon_async_list list = { NULL, 0xfffffffe };
    struct horizon_async *a, *b, *c;
    int read, write;

    /* An id is never 0, which the client takes for "nothing to wait on". */
    assert( horizon_async_new_id( &list ) == 0xffffffff );
    assert( horizon_async_new_id( &list ) == 1 );

    /* Two recvs and a send on one socket: the recvs complete in order. */
    a = make( 8, HORIZON_ASYNC_READ, HORIZON_ASYNC_QUEUED ); a->id = 10; a->owner_tid = 4;
    b = make( 8, HORIZON_ASYNC_READ, HORIZON_ASYNC_QUEUED ); b->id = 11; b->owner_tid = 4;
    c = make( 8, HORIZON_ASYNC_WRITE, HORIZON_ASYNC_QUEUED ); c->id = 12; c->owner_tid = 20;
    horizon_async_add( &list, a );
    horizon_async_add( &list, b );
    horizon_async_add( &list, c );
    assert( horizon_async_next_queued( &list, 8, HORIZON_ASYNC_READ ) == a );
    assert( horizon_async_next_queued( &list, 8, HORIZON_ASYNC_WRITE ) == c );
    assert( horizon_async_waiting_on( &list, 8, &read, &write ) && read && write );
    assert( !horizon_async_waiting_on( &list, 9, &read, &write ) );

    assert( horizon_async_any_queued( &list ) );

    /* While the first is with its thread, the second must not overtake it. */
    horizon_async_ready( &list, a, HORIZON_ASYNC_STATUS_ALERTED, 1000 );
    assert( list.ready == 1 );
    assert( !horizon_async_next_queued( &list, 8, HORIZON_ASYNC_READ ) );
    assert( horizon_async_ready_for( &list, 4, 1000, 200 ) == a );
    /* Another thread gets it only once it has waited for its own too long. */
    assert( !horizon_async_ready_for( &list, 20, 1199, 200 ) );
    assert( !horizon_async_any_stale( &list, 1199, 200 ) );
    assert( horizon_async_ready_for( &list, 20, 1200, 200 ) == a );
    assert( horizon_async_any_stale( &list, 1200, 200 ) );
    horizon_async_run( &list, a, 77 );
    assert( !list.ready && a->state == HORIZON_ASYNC_RUNNING );
    assert( !horizon_async_ready_for( &list, 4, 5000, 200 ) );
    assert( horizon_async_find_apc( &list, 77 ) == a );
    assert( !horizon_async_find_apc( &list, 0 ) );
    assert( !horizon_async_next_queued( &list, 8, HORIZON_ASYNC_READ ) );
    horizon_async_remove( &list, a );
    horizon_async_free( a );
    assert( horizon_async_next_queued( &list, 8, HORIZON_ASYNC_READ ) == b );
    assert( horizon_async_find_id( &list, 11 ) == b && !horizon_async_find_id( &list, 0 ) );
    /* get_async_result names one by the client's own pointer to it. */
    b->data.user = 0x7fee1000;
    assert( horizon_async_find_user( &list, 0x7fee1000 ) == b && !horizon_async_find_user( &list, 1 ) );

    /* The completion rule, case by case. */
    {
        struct horizon_async t;

        memset( &t, 0, sizeof(t) );
        t.data.apc_context = 0x1000;   /* the OVERLAPPED, as ws2_32 passes it */

        /* Failed at once: the program was told then, and nothing else happens. */
        t.pending = 0;
        assert( horizon_async_completion( &t, 1, 1, 0 ) == 0 );
        /* Failed after going pending: it is waiting to hear, so it hears. */
        t.pending = 1;
        assert( horizon_async_completion( &t, 1, 1, 0 ) == HORIZON_ASYNC_POST );
        /* Done at once on a socket that skips that: Asio handles it inline. */
        t.pending = 0;
        assert( horizon_async_completion( &t, 0, 1, 1 ) == 0 );
        assert( horizon_async_completion( &t, 0, 1, 0 ) == HORIZON_ASYNC_POST );
        /* Pending, then done: always posted, whatever the socket asked. */
        t.pending = 1;
        assert( horizon_async_completion( &t, 0, 1, 1 ) == HORIZON_ASYNC_POST );
        /* No port, no packet; no context, nothing to post it with. */
        assert( horizon_async_completion( &t, 0, 0, 0 ) == 0 );
        t.data.apc_context = 0;
        assert( horizon_async_completion( &t, 0, 1, 0 ) == 0 );
        /* A completion routine instead of the port, and the event either way. */
        t.data.apc_context = 0x1000;
        t.data.apc = 0x2000;
        t.data.event = 0x40;
        assert( horizon_async_completion( &t, 0, 1, 0 ) == (HORIZON_ASYNC_APC | HORIZON_ASYNC_EVENT) );
        t.data.apc = 0;
        assert( horizon_async_completion( &t, 0, 1, 0 ) == (HORIZON_ASYNC_POST | HORIZON_ASYNC_EVENT) );
    }

    /* Cancelling: what waits ends as cancelled, through its thread; what the
     * client is trying or running now ends by itself. */
    b->data.iosb = 0x5000; b->owner_tid = 4;
    c->data.iosb = 0x6000;
    assert( horizon_async_cancel( &list, 8, 0x6000, 0, 10, 0 ) == 1 );
    assert( c->state == HORIZON_ASYNC_READY && c->status == HORIZON_ASYNC_STATUS_CANCELLED );
    assert( b->state == HORIZON_ASYNC_QUEUED );
    c->state = HORIZON_ASYNC_RUNNING;
    assert( horizon_async_cancel( &list, 8, 0, 20, 10, 0 ) == 0 );   /* CancelIo from another thread */
    assert( horizon_async_cancel( &list, 8, 0, 4, 10, 0 ) == 1 );    /* and from the one that started it */
    assert( b->status == HORIZON_ASYNC_STATUS_CANCELLED );
    /* Closed rather than cancelled, a recv ends as free_async_queue ends it. */
    b->state = HORIZON_ASYNC_QUEUED;
    assert( horizon_async_cancel( &list, 8, 0, 0, 10, 1 ) == 1 );
    assert( b->status == HORIZON_ASYNC_STATUS_HANDLES_CLOSED );
    /* Closing the socket accepted into ends the AcceptEx waiting on another. */
    a = make( 30, HORIZON_ASYNC_READ, HORIZON_ASYNC_QUEUED );
    a->kind = HORIZON_ASYNC_ACCEPT_INTO; a->accept_into = 9;
    horizon_async_add( &list, a );
    assert( horizon_async_cancel( &list, 9, 0, 0, 10, 1 ) == 1 && a->state == HORIZON_ASYNC_READY );
    assert( a->status == HORIZON_ASYNC_STATUS_CANCELLED );
    assert( horizon_async_cancel( &list, 7, 0, 0, 10, 1 ) == 0 );
    horizon_async_remove( &list, a );
    horizon_async_remove( &list, b );
    horizon_async_remove( &list, c );
    assert( !list.head && !horizon_async_any_queued( &list ) );
    horizon_async_free( a );
    horizon_async_free( b );
    horizon_async_free( c );

    /* What AcceptEx is given: 16 bytes of data asked for, then two addresses. */
    {
        unsigned int local_at, remote_at, remote_len;

        assert( horizon_async_accept_layout( 16 + 32 + 32, 16, 32, &local_at, &remote_at, &remote_len ) );
        assert( local_at == 16 && remote_at == 48 && remote_len == 32 );
        assert( horizon_async_accept_layout( 64, 0, 32, &local_at, &remote_at, &remote_len ) && local_at == 0 );
        assert( !horizon_async_accept_layout( 16, 16, 32, &local_at, &remote_at, &remote_len ) );
        assert( !horizon_async_accept_layout( 64, 0, 2, &local_at, &remote_at, &remote_len ) );
        assert( !horizon_async_accept_layout( 34, 0, 32, &local_at, &remote_at, &remote_len ) );
    }

    printf( "socket asyncs: order kept, one at a time, run by its thread or a stale one's taker, cancelled through the client, and async_set_result's rule\n" );
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'async.c'
    source.write_text(fixture.replace('@HEADER@', header))
    binary = Path(tmp) / 'async'
    subprocess.run(['cc', '-Wall', '-Werror', '-o', str(binary), str(source)], check=True)
    print(subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.strip())
