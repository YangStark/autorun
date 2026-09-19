/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * What Asio does on Windows, which is what The Sims 2 Legacy's launcher
 * emulation and the game's own LSX client are written on: sockets tied to an
 * I/O completion port, an acceptor made with SO_REUSEADDR, AcceptEx and
 * ConnectEx, overlapped WSARecv and WSASend, and GetQueuedCompletionStatus for
 * every result. Closing a socket must end what waits on it, which Wine does
 * with STATUS_HANDLES_CLOSED (676 from GetQueuedCompletionStatus). A plain
 * blocking accept() comes next.
 *
 * Then what EA's DirtySock does, which the game itself talks to the launcher
 * emulation with: IPv6 sockets used dual-stack. A datagram socket bound to
 * ::ffff:0:0 waits in an overlapped WSARecvFrom that its thread polls with
 * GetOverlappedResult between sleeps, and a stream socket reaches the IPv4
 * listener at ::ffff:127.0.0.1.
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

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW( IOC_VENDOR, 12 )
#endif

static char report[1536];
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

/* ::ffff:127.0.0.1, or ::ffff:0:0 for any address. */
static void mapped( struct sockaddr_in6 *six, unsigned short port, int loopback )
{
    memset( six, 0, sizeof(*six) );
    six->sin6_family = AF_INET6;
    six->sin6_port = port;
    six->sin6_addr.s6_addr[10] = six->sin6_addr.s6_addr[11] = 0xff;
    if (loopback)
    {
        six->sin6_addr.s6_addr[12] = 127;
        six->sin6_addr.s6_addr[15] = 1;
    }
}

static int is_mapped_loopback( const struct sockaddr_in6 *six )
{
    return six->sin6_family == AF_INET6 && six->sin6_addr.s6_addr[10] == 0xff &&
           six->sin6_addr.s6_addr[11] == 0xff && six->sin6_addr.s6_addr[12] == 127 &&
           six->sin6_addr.s6_addr[15] == 1;
}

/* DirtySock's two sockets, against the IPv4 listener at port (network order). */
static void dirtysock( SOCKET listener, unsigned short port )
{
    struct sockaddr_in6 six, name6, from6;
    SOCKET udp, tcp, strict, peer;
    WSAOVERLAPPED ov;
    DWORD bytes, flags;
    WSABUF buf;
    char dgram[16];
    u_long on = 1;
    int off = 0, len, from_len, i, rc;
    BOOL no = FALSE, yes = TRUE;
    fd_set writable;
    struct timeval tv = { 2, 0 };

    udp = WSASocketW( AF_INET6, SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED );
    if (udp == INVALID_SOCKET)
    {
        say( "\nIPv6 udp=FAIL(%d)", WSAGetLastError() );
        return;
    }
    ioctlsocket( udp, FIONBIO, &on );
    setsockopt( udp, SOL_SOCKET, SO_BROADCAST, (char *)&yes, sizeof(yes) );
    say( "\nIPv6 udp: v6only=%s", setsockopt( udp, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&off, sizeof(off) ) ?
         "FAIL" : "0" );
    WSAIoctl( udp, SIO_UDP_CONNRESET, &no, sizeof(no), NULL, 0, &bytes, NULL, NULL );
    mapped( &six, 0, 0 );
    len = sizeof(name6);
    if (bind( udp, (struct sockaddr *)&six, sizeof(six) ) || getsockname( udp, (struct sockaddr *)&name6, &len ))
        say( " bind=FAIL(%d)", WSAGetLastError() );
    else
        say( " bind=%d/%d", name6.sin6_family, len );

    memset( &ov, 0, sizeof(ov) );
    ov.hEvent = WSACreateEvent();
    buf.buf = dgram;
    buf.len = sizeof(dgram);
    flags = 0;
    from_len = sizeof(from6);
    memset( &from6, 0, sizeof(from6) );
    rc = WSARecvFrom( udp, &buf, 1, NULL, &flags, (struct sockaddr *)&from6, &from_len, &ov, NULL );
    say( " WSARecvFrom=%s", !rc ? "at-once" : WSAGetLastError() == WSA_IO_PENDING ? "pending" : "FAIL" );
    mapped( &six, name6.sin6_port, 1 );
    say( " sendto=%d", sendto( udp, "wake", 4, 0, (struct sockaddr *)&six, sizeof(six) ) );
    /* GetOverlappedResult without waiting, and a sleep between: no wait of this
     * thread's own ever runs the read. */
    for (i = 0; i < 200 && !WSAGetOverlappedResult( udp, &ov, &bytes, FALSE, &flags ); i++) Sleep( 10 );
    say( " got=%lu@%dms from=", i < 200 ? bytes : 0, i * 10 );
    if (i < 200 && is_mapped_loopback( &from6 )) say( "::ffff:127.0.0.1" );
    else
    {
        char text[64] = "?";
        DWORD text_len = sizeof(text);

        WSAAddressToStringA( (struct sockaddr *)&from6, from_len, NULL, text, &text_len );
        say( "%s(family %d, %d bytes)", text, from6.sin6_family, from_len );
    }
    WSACloseEvent( ov.hEvent );
    closesocket( udp );

    /* Windows makes an IPv6 socket IPv6 only: IPv4 written the IPv6 way is not reached. */
    strict = WSASocketW( AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED );
    mapped( &six, port, 1 );
    rc = connect( strict, (struct sockaddr *)&six, sizeof(six) );
    say( "\nIPv6 tcp: v6only-default=%d", rc ? WSAGetLastError() : 0 );
    closesocket( strict );

    tcp = WSASocketW( AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED );
    ioctlsocket( tcp, FIONBIO, &on );
    setsockopt( tcp, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&off, sizeof(off) );
    memset( &six, 0, sizeof(six) );
    six.sin6_family = AF_INET6;
    bind( tcp, (struct sockaddr *)&six, sizeof(six) );
    mapped( &six, port, 1 );
    rc = connect( tcp, (struct sockaddr *)&six, sizeof(six) );
    say( " connect=%s", !rc ? "at-once" : WSAGetLastError() == WSAEWOULDBLOCK ? "wouldblock" : "FAIL" );
    FD_ZERO( &writable );
    FD_SET( tcp, &writable );
    say( " writable=%d", select( 0, NULL, &writable, NULL, &tv ) );
    peer = accept( listener, NULL, NULL );
    say( " accept()=%s", peer != INVALID_SOCKET ? "ok" : "FAIL" );
    if (peer != INVALID_SOCKET)
    {
        send( peer, "<LSX>", 5, 0 );
        for (i = 0; i < 200 && (rc = recv( tcp, dgram, sizeof(dgram), 0 )) < 0; i++) Sleep( 10 );
        say( " recv=%d", rc );
        closesocket( peer );
    }
    len = sizeof(name6);
    say( " peer=%s", !getpeername( tcp, (struct sockaddr *)&name6, &len ) && is_mapped_loopback( &name6 ) &&
         name6.sin6_port == port ? "::ffff:127.0.0.1" : "WRONG" );
    closesocket( tcp );
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

    dirtysock( listener, addr.sin_port );

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
