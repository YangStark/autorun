#!/usr/bin/env python3
"""Write the deterministic folder tree that "7zr a" archives on Switch.

The two payloads are the files inside samples/7zr-x86/7zr-sample.7z, so their
CRCs are already recorded there. Usage: make-7zr-tree.py <drive_c directory>
"""
from pathlib import Path
import sys
import zlib

README = (b"Wine-NX 7-Zip folder tree.\n"
          b"\"7zr a C:\\wine-nx-tree.7z C:\\7zr-tree -mx1\" archives this folder;\n"
          b"7zr must find 3 folders and 3 files while scanning the drive.\n")


def sample_text():
    return b"".join(b"Line %05d: The quick brown fox jumps over the lazy dog %08x\n"
                    % (i, (i * 0x9e3779b1) & 0xffffffff) for i in range(4000))


def sample_bin():
    state, out = 12345, bytearray()
    for _ in range(65536):
        state = (1103515245 * state + 12345) & 0x7fffffff
        out.append(state >> 23)
    return bytes(out)


# Relative path, contents and CRC-32. The verifier imports this table.
TREE = (
    ("7zr-tree/readme.txt", README, None),
    ("7zr-tree/data/wine-nx-sample.bin", sample_bin(), 0xC2C3F618),
    ("7zr-tree/data/text/wine-nx-sample.txt", sample_text(), 0x59965AA4),
)


def main():
    drive_c = Path(sys.argv[1])
    for name, data, crc in TREE:
        assert crc is None or zlib.crc32(data) == crc, f"generator drifted for {name}"
        path = drive_c / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    total = sum(len(data) for _, data, _ in TREE)
    print(f"7zr tree: 3 folders, {len(TREE)} files, {total} bytes in {drive_c / '7zr-tree'}")


if __name__ == "__main__":
    main()
