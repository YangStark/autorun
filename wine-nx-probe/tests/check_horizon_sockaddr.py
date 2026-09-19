#!/usr/bin/env python3
"""IPv6 sockets on IPv4 (dlls/ntdll/unix/horizon_sockaddr.h).

Horizon's socket service has no IPv6. EA's DirtySock, which The Sims 2 Legacy
reaches its launcher with, opens AF_INET6 sockets, binds :: and connects to
::ffff:127.0.0.1; before this the server refused to create them at all, and the
game said it had not been launched from the EA app. The addresses an IPv6 socket
uses to reach IPv4 are the ones carried over; one with no IPv4 behind it is
not."""
from pathlib import Path
import ipaddress
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = (root / 'dlls/ntdll/unix/horizon_sockaddr.h').read_text()


def ws_in6(address, port, size=28):
    data = struct.pack('<H', 23) + struct.pack('>H', port) + b'\0' * 4 + ipaddress.IPv6Address(address).packed
    return (data + b'\0' * 4)[:size]


cases = [
    ('::', 0, True, '0.0.0.0'),
    ('::ffff:0:0', 0, True, '0.0.0.0'),
    ('::ffff:127.0.0.1', 55334, True, '127.0.0.1'),
    ('::ffff:192.168.1.65', 3216, True, '192.168.1.65'),
    ('::1', 80, True, '127.0.0.1'),
    ('fe80::1', 80, False, None),
    ('2804:4e14::1', 443, False, None),
    ('::2', 1, False, None),
]
rows = []
for address, port, ok, v4 in cases:
    for size in (28, 24):
        data = ws_in6(address, port, size)
        rows.append('{ "%s", { %s }, %d, %d, %d, %s }' % (
            address, ', '.join(str(b) for b in data), size, port, ok,
            '{ %s }' % ', '.join(str(b) for b in ipaddress.IPv4Address(v4).packed) if v4 else '{ 0 }'))

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <arpa/inet.h>

@HEADER@

struct row { const char *name; unsigned char ws[28]; unsigned int size; unsigned short port; int ok; unsigned char v4[4]; };
static const struct row rows[] = { @ROWS@ };

int main( void )
{
    unsigned char port[2], v4[4], ws[28], in6[16];
    unsigned int i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
    {
        int found = horizon_ws_in6_to_v4( rows[i].ws, rows[i].size, port, v4 );

        if (found != rows[i].ok) { printf( "%s: %d\n", rows[i].name, found ); return 1; }
        if (!found) continue;
        assert( ((port[0] << 8) | port[1]) == rows[i].port );
        assert( !memcmp( v4, rows[i].v4, 4 ) );
    }
    /* Too short to be a sockaddr_in6. */
    assert( horizon_ws_in6_to_v4( rows[0].ws, 23, port, v4 ) == -1 );

    /* What an IPv6 socket reports: any address as ::, the rest ::ffff:a.b.c.d. */
    port[0] = 0xd8; port[1] = 0x26;   /* 55334 */
    v4[0] = 127; v4[1] = 0; v4[2] = 0; v4[3] = 1;
    assert( horizon_ws_in6_from_v4( port, v4, ws, 27 ) == 0 );
    assert( horizon_ws_in6_from_v4( port, v4, ws, sizeof(ws) ) == 28 );
    assert( ws[0] == 23 && ws[1] == 0 && ws[2] == 0xd8 && ws[3] == 0x26 );
    assert( ws[18] == 0xff && ws[19] == 0xff && ws[20] == 127 && ws[23] == 1 );
    assert( horizon_ws_in6_to_v4( ws, 28, port, v4 ) == 1 && v4[0] == 127 && v4[3] == 1 );
    memset( v4, 0, 4 );
    horizon_v4_to_in6( v4, in6 );
    assert( !memcmp( in6, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16 ) );
    assert( horizon_in6_is_mapped( ws + 8 ) && !horizon_in6_is_mapped( in6 ) );

    printf( "IPv6 on IPv4: ::, ::ffff:a.b.c.d and ::1 carried over, other IPv6 refused, reported back as IPv6\n" );
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='wine-nx-sockaddr-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(fixture.replace('@HEADER@', header).replace('@ROWS@', ',\n'.join(rows)))
    subprocess.run(['cc', '-Wall', '-Werror', '-fsanitize=address,undefined', str(tmp / 'test.c'),
                    '-o', str(tmp / 'test')], check=True)
    print(subprocess.run([str(tmp / 'test')], check=True, capture_output=True, text=True).stdout.strip())
