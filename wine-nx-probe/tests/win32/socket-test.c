/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * What Asio does on Windows, which is what The Sims 2 Legacy's launcher
 * emulation and the game's own LSX client are written on: sockets tied to an
 * I/O completion port, an acceptor made with SO_REUSEADDR, AcceptEx and
 * ConnectEx, overlapped WSARecv and WSASend, and GetQueuedCompletionStatus for
 * every result. Closing a socket must end what waits on it, which Wine does
 * with STATUS_HANDLES_CLOSED (676 from GetQueuedCompletionStatus). A plain
 * blocking accept() comes last.
 *
 * The result goes in a message box, which the runtime log records, and in
 * socket-test.txt beside the program.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char report[1024];
static size_t used;

static void say( const char *format, ... )
{
    va_list args;

    if (used >= sizeof(report) - 1) return;
    va_start( args, format );
    used += vsnprintf( report + used, sizeof(report) - used, format, args );
    va_end( args );
    if (used > sizeof(report) - 1) used = sizeof(report) - 1;
}

static void *extension( SOCKET s, GUID guid )
{
    void *function = NULL;
    DWORD bytes;

    if (WSAIoctl( s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &function, sizeof(function),
                  &bytes, NULL, NULL ))
        return NULL;
    return function;
}

/* One packet from the port: which operation, how it ended, how many bytes. */
static const char *next_packet( HANDLE port, OVERLAPPED **which, DWORD *bytes, DWORD *error )
{
    ULONG_PTR key;
    DWORD start = GetTickCount();
    BOOL ok;
    static char text[48];

    *which = NULL;
    ok = GetQueuedCompletionStatus( port, bytes, &key, which, 5000 );
    *error = ok ? 0 : GetLastError();
    if (!*which) snprintf( text, sizeof(text), "none(%lu)", *error );
    else snprintf( text, sizeof(text), "%lums", GetTickCount() - start );
    return text;
}

static const char *which_name( OVERLAPPED *which, OVERLAPPED *names[], const char *labels[], int count )
{
    int i;

    for (i = 0; i < count; i++) if (which == names[i]) return labels[i];
    return which ? "?" : "-";
}

int WINAPI WinMain( HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show )
{
    static const GUID accept_ex_guid = WSAID_ACCEPTEX, connect_ex_guid = WSAID_CONNECTEX;
    static const GUID sockaddrs_guid = WSAID_GETACCEPTEXSOCKADDRS;
    LPFN_ACCEPTEX accept_ex;
    LPFN_CONNECTEX connect_ex;
    LPFN_GETACCEPTEXSOCKADDRS get_sockaddrs;
    OVERLAPPED accept_ov, connect_ov, recv_ov, send_ov, abort_ov, *which;
    OVERLAPPED *names[5] = { &accept_ov, &connect_ov, &recv_ov, &send_ov, &abort_ov };
    const char *labels[5] = { "accept", "connect", "recv", "send", "abort" };
    char accept_buffer[2 * (sizeof(struct sockaddr_in) + 16)], data[16];
    struct sockaddr_in addr, any, local, *remote_addr, *local_addr;
    SOCKET listener, acceptor, client, plain, taken;
    int addr_len, remote_len, local_len, i, reuse = 1;
    DWORD bytes, error, flags;
    WSABUF recv_buf, send_buf;
    WSADATA wsa;
    HANDLE port;
    BOOL ok;
    FILE *file;

    WSAStartup( MAKEWORD( 2, 2 ), &wsa );
    port = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0 );

    /* asio::ip::tcp::acceptor( io, endpoint ): open, tie to the port, SO_REUSEADDR, bind, listen. */
    listener = WSASocketW( AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED );
    if (CreateIoCompletionPort( (HANDLE)listener, port, 0, 0 )) say( "iocp=ok" );
    else say( "iocp=FAIL(%lu)", GetLastError() );
    say( " reuse=%s", setsockopt( listener, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse) ) ?
         "FAIL" : "ok" );
    memset( &addr, 0, sizeof(addr) );
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    addr_len = sizeof(addr);
    if (bind( listener, (struct sockaddr *)&addr, sizeof(addr) ) ||
        getsockname( listener, (struct sockaddr *)&addr, &addr_len ) ||
        listen( listener, SOMAXCONN ))
    {
        say( " listen=FAIL(%d)", WSAGetLastError() );
        goto done;
    }
    say( " listen=%u", ntohs( addr.sin_port ) );

    accept_ex = extension( listener, accept_ex_guid );
    connect_ex = extension( listener, connect_ex_guid );
    get_sockaddrs = extension( listener, sockaddrs_guid );
    if (!accept_ex || !connect_ex || !get_sockaddrs)
    {
        say( " extensions=FAIL" );
        goto done;
    }

    /* async_accept: AcceptEx into a new socket, pending until someone connects. */
    acceptor = WSASocketW( AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED );
    memset( &accept_ov, 0, sizeof(accept_ov) );
    ok = accept_ex( listener, acceptor, accept_buffer, 0, sizeof(struct sockaddr_in) + 16,
                    sizeof(struct sockaddr_in) + 16, &bytes, &accept_ov );
    say( "\nAcceptEx=%s", ok ? "at-once" : WSAGetLastError() == WSA_IO_PENDING ? "pending" : "FAIL" );

    /* async_connect: bind to any address, then ConnectEx. */
    client = WSASocketW( AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED );
    CreateIoCompletionPort( (HANDLE)client, port, 1, 0 );
    memset( &any, 0, sizeof(any) );
    any.sin_family = AF_INET;
    bind( client, (struct sockaddr *)&any, sizeof(any) );
    memset( &connect_ov, 0, sizeof(connect_ov) );
    ok = connect_ex( client, (struct sockaddr *)&addr, sizeof(addr), NULL, 0, NULL, &connect_ov );
    say( " ConnectEx=%s", ok ? "at-once" : WSAGetLastError() == WSA_IO_PENDING ? "pending" : "FAIL" );

    /* Both end on the port, whichever came at once. */
    for (i = 0; i < 2; i++)
    {
        const char *when = next_packet( port, &which, &bytes, &error );

        say( " %s:%lu@%s", which_name( which, names, labels, 5 ), error, when );
    }
    setsockopt( acceptor, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&listener, sizeof(listener) );
    setsockopt( client, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0 );
    get_sockaddrs( accept_buffer, 0, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16,
                   (struct sockaddr **)&local_addr, &local_len, (struct sockaddr **)&remote_addr, &remote_len );
    addr_len = sizeof(local);
    getsockname( client, (struct sockaddr *)&local, &addr_len );
    say( " peer=%s", remote_len == sizeof(struct sockaddr_in) && remote_addr->sin_port == local.sin_port &&
         local_addr->sin_port == addr.sin_port ? "ok" : "WRONG" );

    /* async_read_some with nothing there, then async_write from the other end. */
    CreateIoCompletionPort( (HANDLE)acceptor, port, 2, 0 );
    memset( &recv_ov, 0, sizeof(recv_ov) );
    memset( data, 0, sizeof(data) );
    recv_buf.buf = data;
    recv_buf.len = sizeof(data);
    flags = 0;
    ok = !WSARecv( acceptor, &recv_buf, 1, NULL, &flags, &recv_ov, NULL );
    say( "\nWSARecv=%s", ok ? "at-once" : WSAGetLastError() == WSA_IO_PENDING ? "pending" : "FAIL" );
    memset( &send_ov, 0, sizeof(send_ov) );
    send_buf.buf = "hello";
    send_buf.len = 5;
    ok = !WSASend( client, &send_buf, 1, NULL, 0, &send_ov, NULL );
    say( " WSASend=%s", ok ? "at-once" : WSAGetLastError() == WSA_IO_PENDING ? "pending" : "FAIL" );
    for (i = 0; i < 2; i++)
    {
        const char *when = next_packet( port, &which, &bytes, &error );

        say( " %s:%lu,%lub@%s", which_name( which, names, labels, 5 ), error, bytes, when );
    }
    say( " got='%s'", data );

    /* Closing a socket ends what waits on it. */
    memset( &abort_ov, 0, sizeof(abort_ov) );
    flags = 0;
    ok = !WSARecv( acceptor, &recv_buf, 1, NULL, &flags, &abort_ov, NULL );
    say( "\nclose: WSARecv=%s", ok ? "at-once" : WSAGetLastError() == WSA_IO_PENDING ? "pending" : "FAIL" );
    closesocket( acceptor );
    {
        const char *when = next_packet( port, &which, &bytes, &error );

        say( " %s:%lu@%s", which_name( which, names, labels, 5 ), error, when );
    }

    /* A blocking accept() with the connection already there. */
    plain = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    ok = !connect( plain, (struct sockaddr *)&addr, sizeof(addr) );
    taken = accept( listener, NULL, NULL );
    say( "\nconnect=%s accept()=%s", ok ? "ok" : "FAIL", taken != INVALID_SOCKET ? "ok" : "FAIL" );
    if (taken != INVALID_SOCKET)
    {
        send( plain, "ping", 4, 0 );
        memset( data, 0, sizeof(data) );
        say( " recv=%d", recv( taken, data, sizeof(data), 0 ) );
        closesocket( taken );
    }
    closesocket( plain );
    closesocket( client );

done:
    closesocket( listener );
    if ((file = fopen( "socket-test.txt", "w" )))
    {
        fprintf( file, "%s\n", report );
        fclose( file );
    }
    /* "quiet" for a run on a desktop, where the box would wait for someone. */
    if (!strstr( cmdline, "quiet" )) MessageBoxA( NULL, report, "Socket test", MB_OK );
    return 0;
}
