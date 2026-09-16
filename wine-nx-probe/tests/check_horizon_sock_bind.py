#!/usr/bin/env python3
"""Run the Horizon server's socket bind and setsockopt ioctls against real host
sockets.

Fallout New Vegas ships GOG Galaxy, whose RakNet sets five socket options and
then binds. The server implemented none of them, so each returned
STATUS_NOT_IMPLEMENTED (build 110 log) and Galaxy tore its RakNet instance down
mid-startup. Bind must behave as wineserver's IOCTL_AFD_BIND does: reply with
the address the socket ended up on, keeping the requested address with the port
the kernel chose, and refuse a second bind with STATUS_ADDRESS_ALREADY_ASSOCIATED."""
from pathlib import Path
import re
import socket
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/horizon.c').read_text()


def block(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


defines = '\n'.join(line for line in source.splitlines()
                    if re.match(r'#define HORIZON_(STATUS_|WS_AF_INET\b|IOCTL_AFD_(BIND|WINE_SET_))', line))

fixture = f'''
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
{defines}

static void horizon_trace( const char *format, ... ) {{ (void)format; }}

/* One socket stands in for the server's object table. */
struct horizon_server_object {{ int file_fd; int sock_bound; }};
static struct horizon_server_object test_socket = {{ -1, 0 }};
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int horizon_server_find_sock_locked( unsigned int handle,
                                                     struct horizon_server_object **object )
{{
    *object = NULL;
    if (handle != 1) return HORIZON_STATUS_INVALID_HANDLE;
    *object = &test_socket;
    return HORIZON_STATUS_SUCCESS;
}}

static unsigned int horizon_server_get_sock_fd( unsigned int handle, int *fd, int *nonblocking )
{{
    struct horizon_server_object *object;
    unsigned int status = horizon_server_find_sock_locked( handle, &object );

    if (status) return status;
    if (object->file_fd == -1) return HORIZON_STATUS_INVALID_HANDLE;
    *fd = object->file_fd;
    if (nonblocking) *nonblocking = 0;
    return HORIZON_STATUS_SUCCESS;
}}

{block('static unsigned int horizon_sock_errno_status(')}
{block('static unsigned int horizon_ws_sockaddr_to_unix(')}
{block('static unsigned int horizon_ws_sockaddr_from_unix(')}
{block('static unsigned int horizon_sock_ioctl_bind(')}
{block('static unsigned int horizon_sock_ioctl_setsockopt(')}

/* A Windows sockaddr_in: 16-bit family, port and address in network order. */
static void ws_addr( unsigned char *params, unsigned short family, unsigned short port,
                     const char *address )
{{
    struct in_addr in;

    memset( params, 0, 20 );
    memcpy( params + 4, &family, sizeof(family) );
    port = htons( port );
    memcpy( params + 6, &port, sizeof(port) );
    inet_aton( address, &in );
    memcpy( params + 8, &in, sizeof(in) );
}}

static unsigned int reply_port( const unsigned char *out )
{{
    unsigned short port;
    memcpy( &port, out + 2, sizeof(port) );
    return ntohs( port );
}}

int main( void )
{{
    unsigned char params[20], out[16];
    unsigned int out_size, status, bound_port;
    int value, second;
    struct linger linger_value;
    socklen_t len;

    test_socket.file_fd = socket( AF_INET, SOCK_DGRAM, 0 );

    /* Options come before the bind, the order RakNet uses. */
    value = 1;
    printf( "broadcast %08x\\n",
            horizon_sock_ioctl_setsockopt( HORIZON_IOCTL_AFD_WINE_SET_SO_BROADCAST, 1,
                                           (unsigned char *)&value, sizeof(value) ) );
    len = sizeof(value); value = 0;
    getsockopt( test_socket.file_fd, SOL_SOCKET, SO_BROADCAST, &value, &len );
    printf( "broadcast-value %d\\n", value != 0 );

    value = 65536;
    printf( "rcvbuf %08x\\n",
            horizon_sock_ioctl_setsockopt( HORIZON_IOCTL_AFD_WINE_SET_SO_RCVBUF, 1,
                                           (unsigned char *)&value, sizeof(value) ) );
    printf( "sndbuf %08x\\n",
            horizon_sock_ioctl_setsockopt( HORIZON_IOCTL_AFD_WINE_SET_SO_SNDBUF, 1,
                                           (unsigned char *)&value, sizeof(value) ) );

    /* Windows LINGER is two 16-bit fields where BSD has two ints. */
    {{
        unsigned short linger_params[2] = {{ 1, 7 }};
        printf( "linger %08x\\n",
                horizon_sock_ioctl_setsockopt( HORIZON_IOCTL_AFD_WINE_SET_SO_LINGER, 1,
                                               (unsigned char *)linger_params, sizeof(linger_params) ) );
    }}
    len = sizeof(linger_value);
    memset( &linger_value, 0, sizeof(linger_value) );
    getsockopt( test_socket.file_fd, SOL_SOCKET, SO_LINGER, &linger_value, &len );
    printf( "linger-value %d %d\\n", (int)linger_value.l_onoff, (int)linger_value.l_linger );

    value = 1;
    printf( "hdrincl %08x\\n",
            horizon_sock_ioctl_setsockopt( HORIZON_IOCTL_AFD_WINE_SET_IP_HDRINCL, 1,
                                           (unsigned char *)&value, sizeof(value) ) );
    value = 0;
    printf( "short-option %08x\\n",
            horizon_sock_ioctl_setsockopt( HORIZON_IOCTL_AFD_WINE_SET_SO_RCVBUF, 1,
                                           (unsigned char *)&value, 2 ) );

    /* Port 0: the kernel picks one, and the reply carries it. */
    ws_addr( params, HORIZON_WS_AF_INET, 0, "127.0.0.1" );
    out_size = 0;
    status = horizon_sock_ioctl_bind( 1, params, sizeof(params), out, sizeof(out), &out_size );
    bound_port = reply_port( out );
    printf( "bind %08x size=%u port=%u ip=%u.%u.%u.%u\\n", status, out_size, bound_port,
            out[4], out[5], out[6], out[7] );

    /* Windows refuses a second bind on the same socket. */
    ws_addr( params, HORIZON_WS_AF_INET, 0, "127.0.0.1" );
    out_size = 0;
    printf( "rebind %08x\\n",
            horizon_sock_ioctl_bind( 1, params, sizeof(params), out, sizeof(out), &out_size ) );

    /* A port already in use, on a second socket. */
    second = socket( AF_INET, SOCK_DGRAM, 0 );
    test_socket.file_fd = second;
    test_socket.sock_bound = 0;
    ws_addr( params, HORIZON_WS_AF_INET, (unsigned short)bound_port, "127.0.0.1" );
    out_size = 0;
    printf( "busy %08x\\n",
            horizon_sock_ioctl_bind( 1, params, sizeof(params), out, sizeof(out), &out_size ) );

    /* A family libnx has no sockets for, and a payload without an address. */
    ws_addr( params, 23 /* AF_INET6 */, 0, "127.0.0.1" );
    out_size = 0;
    printf( "family %08x\\n",
            horizon_sock_ioctl_bind( 1, params, sizeof(params), out, sizeof(out), &out_size ) );
    ws_addr( params, HORIZON_WS_AF_INET, 0, "127.0.0.1" );
    out_size = 0;
    printf( "short %08x\\n", horizon_sock_ioctl_bind( 1, params, 8, out, sizeof(out), &out_size ) );

    /* No room for the address is not a failure: the socket is still bound. */
    out_size = 0;
    status = horizon_sock_ioctl_bind( 1, params, sizeof(params), out, 0, &out_size );
    printf( "no-room %08x size=%u\\n", status, out_size );

    close( second );
    return 0;
}}
'''

with tempfile.TemporaryDirectory(prefix='wine-nx-sock-bind-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(fixture)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    out = subprocess.run([str(tmp / 'test')], check=True, capture_output=True, text=True).stdout.splitlines()

rows = {line.split()[0]: line.split()[1:] for line in out}
NOT_IMPLEMENTED = '0000000c0000002'[-8:]

for name in ('broadcast', 'rcvbuf', 'sndbuf', 'linger', 'bind', 'no-room'):
    assert rows[name][0] == '00000000', (name, rows[name])
assert rows['broadcast-value'] == ['1'], rows['broadcast-value']
assert rows['linger-value'] == ['1', '7'], rows['linger-value']
# IP_HDRINCL on a datagram socket may be refused, but never as "not implemented".
assert rows['hdrincl'][0] != NOT_IMPLEMENTED, rows['hdrincl']
assert rows['short-option'][0] == '%08x' % 0xc000000d, rows['short-option']

size, port, ip = rows['bind'][1:]
assert size == 'size=16' and ip == 'ip=127.0.0.1', rows['bind']
assert int(port.removeprefix('port=')) > 0, rows['bind']
assert rows['rebind'][0] == '%08x' % 0xc0000238, rows['rebind']
assert rows['busy'][0] == '%08x' % 0xc0000043, rows['busy']
assert rows['family'][0] == '%08x' % 0xc00000bb, rows['family']
assert rows['short'][0] == '%08x' % 0xc000000d, rows['short']
assert rows['no-room'][1] == 'size=0', rows['no-room']

print('Socket bind: options set, port assigned and reported, second bind, busy port, '
      'bad family and short requests refused as Windows does')
