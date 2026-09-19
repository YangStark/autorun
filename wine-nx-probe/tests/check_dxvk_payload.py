#!/usr/bin/env python3
"""Reject stale, corrupt or wrong-architecture DXVK payloads before packaging."""
import json
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from dxvk_payload import DLLS, REVISION, VERSION, digest, validate_payload


with tempfile.TemporaryDirectory(prefix='wine-nx-dxvk-') as temp:
    directory = Path(temp)
    image = bytearray(256)
    image[:2] = b'MZ'
    struct.pack_into('<I', image, 0x3c, 128)
    image[128:132] = b'PE\0\0'
    struct.pack_into('<H', image, 132, 0x8664)
    struct.pack_into('<H', image, 150, 0x2000)
    struct.pack_into('<H', image, 152, 0x20b)
    for name in DLLS:
        (directory / name).write_bytes(image)
    (directory / 'licenses').mkdir()
    (directory / 'licenses/test.txt').write_text('test fixture')
    manifest = dict(version=VERSION, revision=REVISION, architecture='x86_64',
                    files={name: digest(directory / name) for name in DLLS}, licenses=['test.txt'])

    def save():
        (directory / 'dxvk-manifest.json').write_text(json.dumps(manifest))

    def rejects():
        try:
            validate_payload(directory)
        except (ValueError, FileNotFoundError):
            return
        raise AssertionError('accepted an invalid payload')

    save()
    assert validate_payload(directory)['version'] == VERSION
    manifest['revision'] = '0' * 40
    save()
    rejects()
    manifest['revision'] = REVISION
    save()
    dll = directory / 'd3d11.dll'
    dll.write_bytes(image + b'corrupt')
    rejects()
    for offset, value in ((132, 0x14c), (152, 0x10b), (150, 0)):
        wrong = image.copy()
        struct.pack_into('<H', wrong, offset, value)
        dll.write_bytes(wrong)
        manifest['files']['d3d11.dll'] = digest(dll)
        save()
        rejects()
    dll.write_bytes(b'MZ')
    rejects()
    dll.write_bytes(image)
    manifest['files']['d3d11.dll'] = digest(dll)
    manifest['licenses'] = ['../outside']
    save()
    rejects()
    manifest['licenses'] = []
    save()
    rejects()

print('PASS: pinned DXVK payload, AMD64 machine, PE32+, DLL bit, hashes and licenses')
