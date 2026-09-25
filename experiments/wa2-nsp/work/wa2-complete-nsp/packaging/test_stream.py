"""Sparse-file checks for 64-bit bundle and NSP layout without large writes."""
import struct
import hashlib
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_nsp import pfs0_header, validate_bundle_bounds
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from validate_cnmt import validate


def tiny_pfs0(entries):
    strings = b"".join(name.encode("ascii") + b"\0" for name, _ in entries)
    strings += bytes((-len(strings)) % 0x20)
    table = bytearray()
    offset = name_offset = 0
    for name, body in entries:
        table.extend(struct.pack("<QQII", offset, len(body), name_offset, 0))
        offset += len(body)
        name_offset += len(name) + 1
    return struct.pack("<4sIII", b"PFS0", len(entries), len(strings), 0) + table + strings + b"".join(
        body for _, body in entries)


class StreamingLayoutTests(unittest.TestCase):
    def test_streaming_cnmt_validation(self):
        program = bytes(0xc00) + b"program"
        control = bytes(0xc00) + b"control"
        record = bytearray()
        for body, kind in ((program, 1), (control, 3)):
            digest = hashlib.sha256(body).digest()
            record += digest + digest[:16] + len(body).to_bytes(6, "little") + bytes([kind, 0])
        cnmt = bytearray(0x30)
        struct.pack_into("<QIB", cnmt, 0, 0x0500A17E00070000, 0, 0x80)
        struct.pack_into("<HHH", cnmt, 0xe, 0x10, 2, 0)
        struct.pack_into("<Q", cnmt, 0x20, 0x0500A17E00070800)
        cnmt += record
        cnmt += hashlib.sha256(cnmt).digest()
        meta = bytes(0xc00) + tiny_pfs0([("Application_0500A17E00070000.cnmt", bytes(cnmt))])
        bodies = [program, control, meta]
        names = [hashlib.sha256(body).hexdigest()[:32] + suffix for body, suffix in
                 zip(bodies, (".nca", ".nca", ".cnmt.nca"))]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "tiny.nsp"
            path.write_bytes(tiny_pfs0(list(zip(names, bodies))))
            validate(path)
            data = bytearray(path.read_bytes())
            data[-1] ^= 1
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "content hash"):
                validate(path)

    @unittest.skipIf(os.name == "nt", "large sparse files are tested on Linux")
    def test_seven_gib_bundle_extents_and_nsp_offsets(self):
        gib = 1024 ** 3
        sizes = [3 * gib, 3 * gib, gib]
        names = [b"wine-nx-runtime.nro", b"drive_c/RuntimeProbe/probe.exe", b"data.bin"]
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            bundle = root / "bundle.bin"
            with bundle.open("wb") as target:
                target.write(struct.pack("<8sII32s", b"ARPBNDL2", 2, 3, b"autorun-games/runtime-probe"))
                offset = 48 + 3 * 240
                for name, size in zip(names, sizes):
                    target.write(struct.pack("<192sQQ32s", name, offset, size, bytes(32)))
                    offset += size
                target.truncate(offset)
            validate_bundle_bounds(bundle)
            # Three sparse NCAs: inspect only the PFS0 table, never their data.
            ncas = []
            for index, size in enumerate(sizes):
                path = root / f"{index}.nca"
                with path.open("wb") as target:
                    target.truncate(size)
                ncas.append((f"{index}.nca", path))
            header = pfs0_header(ncas)
            first = struct.unpack_from("<QQ", header, 16)
            second = struct.unpack_from("<QQ", header, 16 + 24)
            third = struct.unpack_from("<QQ", header, 16 + 48)
            self.assertEqual(first, (0, 3 * gib))
            self.assertEqual(second, (3 * gib, 3 * gib))
            self.assertEqual(third, (6 * gib, gib))

    @unittest.skipIf(os.name == "nt", "large sparse files are tested on Linux")
    def test_rejects_fat32_oversized_entry(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bundle.bin"
            length = 0x100000000
            with path.open("wb") as target:
                target.write(struct.pack("<8sII32s", b"ARPBNDL2", 2, 1, bytes(32)))
                target.write(struct.pack("<192sQQ32s", b"wine-nx-runtime.nro", 288, length, bytes(32)))
                target.truncate(288 + length)
            with self.assertRaisesRegex(ValueError, "FAT32"):
                validate_bundle_bounds(path)


if __name__ == "__main__":
    unittest.main()
