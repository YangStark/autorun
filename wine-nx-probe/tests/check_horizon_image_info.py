#!/usr/bin/env python3
"""Run the Horizon server's PE image reader (horizon_server_read_pe_image_info)
on synthetic images, and on a real one named by WINE_NX_IMAGE_INFO_EXE.

It must size images as wineserver's get_image_params does: SizeOfImage rounded
up to the section alignment, at least a page, and the header mapping ending at
the first section. NFS Most Wanted Black Edition's speed.exe has a SizeOfImage
that ends inside its last section's page, and loading it failed with
STATUS_INVALID_IMAGE_FORMAT."""
from pathlib import Path
import os
import re
import struct
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


struct_start = source.index('struct horizon_pe_image_info\n{')
struct_text = source[struct_start:source.index('};', struct_start) + 2]
defines = '\n'.join(line for line in source.splitlines()
                    if re.match(r'#define HORIZON_(IMAGE_|STATUS_(SUCCESS|NO_MEMORY|INVALID_IMAGE_FORMAT))', line))

fixture = f'''
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
{defines}
{struct_text}
static unsigned short horizon_process_machine = HORIZON_IMAGE_FILE_MACHINE_I386;
static unsigned int horizon_server_errno_status( int error ) {{ return 0xc0000000u | (unsigned int)error; }}
{block('static unsigned int horizon_server_read_exact_at(')}
{block('static unsigned short horizon_get_le16(')}
{block('static unsigned int horizon_get_le32(')}
{block('static unsigned long long horizon_get_le64(')}
{block('static unsigned int horizon_server_read_pe_image_info(')}
int main( int argc, char **argv )
{{
    int i;

    for (i = 1; i < argc; i++)
    {{
        struct horizon_pe_image_info info;
        int fd = open( argv[i], O_RDONLY );
        unsigned int status = horizon_server_read_pe_image_info( fd, &info );

        printf( "%d %08x %x %x %x %x\\n", i - 1, status, info.map_size, info.header_map_size,
                info.image_flags, info.alignment );
        close( fd );
    }}
    return 0;
}}
'''


def pe32(path, size_of_image, sections, section_alignment=0x1000, size_of_headers=0x1000):
    """An i386 PE: sections are (virtual address, virtual size, raw pointer, raw size)."""
    opt = bytearray(224)
    struct.pack_into('<H', opt, 0, 0x10b)
    struct.pack_into('<I', opt, 16, 0x1000)                      # entry point
    struct.pack_into('<I', opt, 28, 0x400000)                    # image base
    struct.pack_into('<II', opt, 32, section_alignment, 0x200)  # section, file alignment
    struct.pack_into('<III', opt, 56, size_of_image, size_of_headers, 0)
    struct.pack_into('<H', opt, 68, 2)
    struct.pack_into('<I', opt, 92, 16)
    headers = bytearray(0x40)
    headers[0:2] = b'MZ'
    struct.pack_into('<I', headers, 0x3c, 0x40)
    headers += b'PE\0\0' + struct.pack('<HHIIIHH', 0x14c, len(sections), 0, 0, 0, len(opt), 0x10f) + opt
    for i, (va, vsize, raw, rawsize) in enumerate(sections):
        headers += struct.pack('<8sIIIIIIHHI', b'.s%d' % i, vsize, va, rawsize, raw, 0, 0, 0, 0,
                               0x60000020 if i == 0 else 0x40000040)
    end = max([0x1000] + [raw + rawsize for _, _, raw, rawsize in sections])
    path.write_bytes(bytes(headers) + bytes(end - len(headers)))


with tempfile.TemporaryDirectory(prefix='wine-nx-image-info-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(fixture)
    subprocess.run(['cc', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)

    # SizeOfImage 0x678e4e: the last section's page runs to 0x679000 (speed.exe's layout).
    pe32(tmp / 'unaligned.exe', 0x678e4e, [(0x1000, 0x48e2a5, 0x1000, 0x1000), (0x638000, 0x40e4e, 0x2000, 0x1000)])
    pe32(tmp / 'aligned.exe', 0x532000, [(0x1000, 0x1000, 0x1000, 0x1000)])
    pe32(tmp / 'align64k.exe', 0x21000, [(0x10000, 0x1000, 0x1000, 0x1000)], section_alignment=0x10000)
    pe32(tmp / 'late-section.exe', 0x4000, [(0x2000, 0x1000, 0x400, 0x200)], size_of_headers=0x400)
    pe32(tmp / 'flat.exe', 0x3000, [(0x400, 0x200, 0x400, 0x200)], section_alignment=0x200)
    pe32(tmp / 'wraps.exe', 0xfffff001, [(0x1000, 0x1000, 0x1000, 0x1000)])
    names = ['unaligned', 'aligned', 'align64k', 'late-section', 'flat', 'wraps']
    args = [str(tmp / (n + '.exe')) for n in names]
    real = os.environ.get('WINE_NX_IMAGE_INFO_EXE')
    if real:
        args.append(real)
    out = subprocess.run([str(tmp / 'test')] + args, check=True, capture_output=True, text=True).stdout.splitlines()
    labels = names + (['real'] if real else [])
    rows = {labels[int(line.split()[0])]: [int(v, 16) for v in line.split()[1:]] for line in out}

    assert rows['unaligned'][:3] == [0, 0x679000, 0x1000], rows['unaligned']
    assert rows['aligned'][:3] == [0, 0x532000, 0x1000], rows['aligned']
    assert rows['align64k'][:3] == [0, 0x30000, 0x10000], rows['align64k']
    assert rows['late-section'][:3] == [0, 0x4000, 0x2000], rows['late-section']
    assert rows['flat'][0] == 0 and rows['flat'][3] & 0x08, rows['flat']
    assert rows['wraps'][0] == 0xc000007b, rows['wraps']
    if real:
        status, map_size, header_map_size = rows['real'][:3]
        assert status == 0 and map_size % 0x1000 == 0 and header_map_size <= map_size, rows['real']
        print(f'{real}: map size {map_size:#x}, header map {header_map_size:#x}')

print('Image info: SizeOfImage rounded to the section alignment, header mapping to the first section, '
      'flat images and overflow passed')
