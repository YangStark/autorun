#!/usr/bin/env python3
"""Write a small AVI for testing quartz's AVI splitter: make_test_avi.py OUT [--no-index] [--absolute-index]

3 seconds of 64x48 RGB24 video at 10 frames per second (stream 0, a colour ramp so frames differ)
and 16-bit mono 8 kHz PCM (stream 1, one chunk per frame), interleaved in movi with an idx1 index.
--no-index leaves out idx1; --absolute-index writes its offsets from the file's start instead of movi.
"""
import struct
import sys

WIDTH, HEIGHT, FRAMES, FPS, RATE = 64, 48, 30, 10, 8000
FRAME_SIZE = WIDTH * HEIGHT * 3
AUDIO_SIZE = RATE // FPS * 2
AVIIF_KEYFRAME = 0x10


def chunk(fourcc, data):
    return fourcc + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')


def lst(kind, data):
    return chunk(b'LIST', kind + data)


def main():
    out = sys.argv[1]
    no_index = '--no-index' in sys.argv
    absolute = '--absolute-index' in sys.argv

    avih = struct.pack('<10I4I', 1000000 // FPS, FRAME_SIZE * FPS, 0, 0x10 if not no_index else 0, FRAMES, 0, 2,
                       FRAME_SIZE, WIDTH, HEIGHT, 0, 0, 0, 0)
    vids = struct.pack('<4s4sIHHIIIIIIII4h', b'vids', b'DIB ', 0, 0, 0, 0, 1, FPS, 0, FRAMES, FRAME_SIZE,
                       0xffffffff, 0, 0, 0, WIDTH, HEIGHT)
    bih = struct.pack('<IiiHHIIiiII', 40, WIDTH, HEIGHT, 1, 24, 0, FRAME_SIZE, 0, 0, 0, 0)
    auds = struct.pack('<4s4sIHHIIIIIIII4h', b'auds', b'\0\0\0\0', 0, 0, 0, 0, 2, RATE * 2, 0,
                       FRAMES * AUDIO_SIZE // 2, AUDIO_SIZE, 0xffffffff, 2, 0, 0, 0, 0)
    wfx = struct.pack('<HHIIHHH', 1, 1, RATE, RATE * 2, 2, 16, 0)
    hdrl = lst(b'hdrl', chunk(b'avih', avih)
               + lst(b'strl', chunk(b'strh', vids) + chunk(b'strf', bih))
               + lst(b'strl', chunk(b'strh', auds) + chunk(b'strf', wfx)))

    movi = b''
    index = []
    for frame in range(FRAMES):
        pixel = bytes((frame * 8 % 256, 255 - frame * 8 % 256, 128))
        for fourcc, data, flags in ((b'00db', pixel * (WIDTH * HEIGHT), AVIIF_KEYFRAME),
                                    (b'01wb', struct.pack('<h', frame * 100) * (AUDIO_SIZE // 2), 0)):
            index.append((fourcc, flags, 4 + len(movi), len(data)))
            movi += chunk(fourcc, data)

    movi_type = 12 + len(hdrl) + 8  # the movi list's type, where offsets count from
    idx1 = b''.join(struct.pack('<4sIII', fourcc, flags, offset + (movi_type if absolute else 0), size)
                    for fourcc, flags, offset, size in index)
    body = b'AVI ' + hdrl + lst(b'movi', movi) + (b'' if no_index else chunk(b'idx1', idx1))
    with open(out, 'wb') as file:
        file.write(b'RIFF' + struct.pack('<I', len(body)) + body)


main()
