import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

from sys import path as sys_path
sys_path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_bundle import bundle, canonical, layout_sizes, MAX_SIZE, MAX_FILES


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = self.root / "payload"
        (self.payload / "drive_c/WA2").mkdir(parents=True)
        (self.payload / "runtime/lib").mkdir(parents=True)
        (self.payload / "wine-nx-runtime.nro").write_bytes(b"nro")
        (self.payload / "drive_c/WA2/WA2_full_menu.exe").write_bytes(b"exe")
        (self.payload / "runtime/lib/a.dll").write_bytes(b"library")

    def make(self):
        output = self.root / "bundle.bin"
        bundle(self.payload, output, self.root / "manifest.json")
        return output.read_bytes()

    def test_nested_integrity_and_offsets(self):
        data = self.make()
        magic, version, count, game = struct.unpack_from("<8sII32s", data)
        self.assertEqual((magic, version, count), (b"ARPBNDL2", 2, 3))
        self.assertEqual(game.rstrip(b"\0"), b"autorun-games/wa2-full")
        expected = {}
        for p in self.payload.rglob("*"):
            if p.is_file():
                expected[p.relative_to(self.payload).as_posix()] = p.read_bytes()
        offset = 48 + 240 * count
        for i in range(count):
            name, start, size, digest = struct.unpack_from("<192sQQ32s", data, 48 + 240 * i)
            name = name.rstrip(b"\0").decode()
            self.assertEqual(start, offset)
            self.assertEqual(data[start:start + size], expected[name])
            self.assertEqual(hashlib.sha256(expected[name]).digest(), digest)
            offset += size
        self.assertEqual(offset, len(data))

    def test_rejects_mutable_state_and_symlink(self):
        (self.payload / "runtime/debug.log").write_text("secret")
        with self.assertRaisesRegex(ValueError, "mutable state"):
            self.make()
        (self.payload / "runtime/debug.log").unlink()
        try:
            (self.payload / "runtime/lib/link").symlink_to(self.payload / "runtime/lib/a.dll")
        except OSError:
            self.skipTest("symlink privilege unavailable")
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.make()

    def test_seed_paths_stay_out_of_immutable_bundle(self):
        for name in ("config/settings.json", "drive_c/WA2/WA2_full_menu.wine-nx.txt"):
            path = self.payload / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("player setting")
            with self.assertRaisesRegex(ValueError, "mutable state"):
                self.make()
            path.unlink()

    def test_pak_is_reserved_for_program_romfs(self):
        pak = self.payload / "drive_c/WA2/fon.pak"
        pak.write_bytes(b"pak")
        with self.assertRaisesRegex(ValueError, "PAK files"):
            self.make()

    def test_rejects_unsafe_name(self):
        (self.payload / "runtime/lib/bad name.dll").write_bytes(b"x")
        with self.assertRaisesRegex(ValueError, "unsafe"):
            self.make()

    def test_large_layout_64_bit_offsets(self):
        offsets, size = layout_sizes([3 * 1024**3, 3 * 1024**3, 1024**3])
        self.assertGreater(offsets[2], 2**32)
        self.assertEqual(size, 7 * 1024**3 + 48 + 3 * 240)
        with self.assertRaisesRegex(ValueError, "FAT32"):
            layout_sizes([2**32])
        with self.assertRaisesRegex(ValueError, "32 GiB"):
            layout_sizes([2**32-1] * 9)
        self.assertEqual(len(layout_sizes([0] * MAX_FILES)[0]), MAX_FILES)
        with self.assertRaisesRegex(ValueError, "count"):
            layout_sizes([0] * (MAX_FILES + 1))

    def test_reserved_names_are_case_insensitive(self):
        for name in ("READY.SHA256", "VERIFY-ALL.FLAG", "a.PART", "../escape"):
            self.assertFalse(canonical(name))

    def test_more_than_old_512_files(self):
        for i in range(520):
            (self.payload / 'runtime/lib' / f'{i:04}.dat').write_bytes(b'x')
        self.assertEqual(struct.unpack_from('<I', self.make(), 12)[0], 523)

    def test_case_insensitive_path_collision(self):
        # Two Linux directory spellings collapse onto one SD-card path.
        if __import__('os').name == 'nt':
            self.skipTest('requires case-sensitive fixture filesystem')
        (self.payload / 'RUNTIME').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.make()


if __name__ == "__main__":
    unittest.main()
