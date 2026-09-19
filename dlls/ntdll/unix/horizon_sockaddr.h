/*
 * IPv6 sockets on a system that has only IPv4.
 *
 * Horizon's socket service knows AF_INET alone, but Windows programs open
 * AF_INET6 sockets and use them dual-stack, reaching IPv4 through addresses
 * of the form ::ffff:a.b.c.d. EA's DirtySock, which The Sims 2 Legacy talks to
 * its launcher with, does nothing else: it binds :: and connects to
 * ::ffff:127.0.0.1. Such a socket is an IPv4 socket underneath; what it sees
 * is written the IPv6 way, and an address with no IPv4 behind it is one this
 * system cannot reach.
 *
 * A Windows sockaddr_in6 is the family, the port, the flow info, the sixteen
 * bytes of address and the scope id: 28 bytes, or 24 in its first form, which
 * had no scope id.
 */
#ifndef WINE_NX_HORIZON_SOCKADDR_H
#define WINE_NX_HORIZON_SOCKADDR_H

#include <string.h>

#define HORIZON_SOCKADDR_WS_INET   2
#define HORIZON_SOCKADDR_WS_INET6  23
#define HORIZON_SOCKADDR_IN6_SIZE  28
#define HORIZON_SOCKADDR_IN6_MIN   24

/* The IPv4 address behind an IPv6 one: :: is any address, ::ffff:a.b.c.d is
 * a.b.c.d, and ::1 is the loopback. Returns 0 for one with no IPv4 behind it. */
static inline int horizon_in6_to_v4( const unsigned char in6[16], unsigned char v4[4] )
{
    static const unsigned char mapped[12] = { 0,0,0,0, 0,0,0,0, 0,0,0xff,0xff };
    static const unsigned char zero[16];

    if (!memcmp( in6, zero, 16 ))
    {
        memset( v4, 0, 4 );
        return 1;
    }
    if (!memcmp( in6, mapped, 12 ))
    {
        memcpy( v4, in6 + 12, 4 );
        return 1;
    }
    if (!memcmp( in6, zero, 15 ) && in6[15] == 1)
    {
        v4[0] = 127; v4[1] = 0; v4[2] = 0; v4[3] = 1;
        return 1;
    }
    return 0;
}

/* Whether the address is written as IPv4 in IPv6 (::ffff:a.b.c.d), which a
 * socket that is IPv6 only does not reach. */
static inline int horizon_in6_is_mapped( const unsigned char in6[16] )
{
    static const unsigned char mapped[12] = { 0,0,0,0, 0,0,0,0, 0,0,0xff,0xff };

    return !memcmp( in6, mapped, 12 );
}

/* An IPv4 address as an IPv6 socket reports it: any address is ::, the rest
 * ::ffff:a.b.c.d. */
static inline void horizon_v4_to_in6( const unsigned char v4[4], unsigned char in6[16] )
{
    memset( in6, 0, 16 );
    if (!v4[0] && !v4[1] && !v4[2] && !v4[3]) return;
    in6[10] = in6[11] = 0xff;
    memcpy( in6 + 12, v4, 4 );
}

/* A Windows sockaddr_in6 for an IPv4 port and address, both in network order.
 * Returns its size, or 0 if it does not fit. */
static inline unsigned int horizon_ws_in6_from_v4( const unsigned char port[2], const unsigned char v4[4],
                                                   unsigned char *ws, unsigned int len )
{
    unsigned short family = HORIZON_SOCKADDR_WS_INET6;

    if (len < HORIZON_SOCKADDR_IN6_SIZE) return 0;
    memset( ws, 0, HORIZON_SOCKADDR_IN6_SIZE );
    memcpy( ws, &family, sizeof(family) );
    memcpy( ws + 2, port, 2 );
    horizon_v4_to_in6( v4, ws + 8 );
    return HORIZON_SOCKADDR_IN6_SIZE;
}

/* The IPv4 port and address a Windows sockaddr_in6 stands for. Returns 1 when
 * there is one, 0 when the address has no IPv4 behind it, -1 when the buffer
 * is too short to be a sockaddr_in6. */
static inline int horizon_ws_in6_to_v4( const unsigned char *ws, unsigned int len,
                                        unsigned char port[2], unsigned char v4[4] )
{
    if (len < HORIZON_SOCKADDR_IN6_MIN) return -1;
    memcpy( port, ws + 2, 2 );
    return horizon_in6_to_v4( ws + 8, v4 );
}

#endif /* WINE_NX_HORIZON_SOCKADDR_H */
