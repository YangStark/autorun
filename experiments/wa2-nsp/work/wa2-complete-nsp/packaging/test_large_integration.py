"""Optional full 7 GiB streaming integration check; run inside the build container."""
import hashlib
import json
import os
from pathlib import Path
import resource
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
GIB = 1024 ** 3
CHUNK = 4 * 1024 * 1024


def read_exact(source, length):
    data = source.read(length)
    if len(data) != length:
        raise ValueError("truncated NSP")
    return data


def main():
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="nsp-stream-", dir="/tmp") as folder:
        base = Path(folder)
        bundle = base / "bundle.bin"
        key = base / "test-header.key"
        asset_root = base / "assets"
        asset_root.mkdir()
        asset_manifest = base / "assets.json"
        rows = []
        for index in range(49):
            name = f"asset-{index:03}.pak"
            body = bytes([index])
            (asset_root / name).write_bytes(body)
            rows.append({"path": name, "pkg": f"wa2-asset-{index:03}.pak",
                         "size": 1, "sha256": hashlib.sha256(body).hexdigest()})
        asset_manifest.write_text(json.dumps(rows))
        out = base / "seven-gib.nsp"
        sizes = [3 * GIB, 3 * GIB, GIB - 48 - 3 * 240]
        names = [b"wine-nx-runtime.nro", b"drive_c/RuntimeProbe/probe.exe", b"data.bin"]
        with bundle.open("wb") as target:
            target.write(struct.pack("<8sII32s", b"ARPBNDL2", 2, 3, b"autorun-games/runtime-probe"))
            offset = 48 + 3 * 240
            for name, size in zip(names, sizes):
                target.write(struct.pack("<192sQQ32s", name, offset, size, bytes(32)))
                offset += size
            target.truncate(offset)
        key.write_bytes(bytes(range(32)))  # Deliberately synthetic; never a console key.
        cmd = [sys.executable, str(ROOT / "packaging/make_nsp.py"),
               "--main", str(ROOT / "build/exefs/main"),
               "--npdm", str(ROOT / "build/exefs/main.npdm"),
               "--icon", str(ROOT / "build/icon.jpg"),
               "--bundle", str(bundle), "--header-key", str(key),
               "--output", str(out), "--atmosphere", "1.6.1",
               "--assets-manifest", str(asset_manifest), "--asset-root", str(asset_root)]
        subprocess.run(cmd, check=True)
        content = []
        with out.open("rb") as source:
            magic, count, string_size, _ = struct.unpack("<4sIII", read_exact(source, 16))
            assert magic == b"PFS0" and count == 3
            table = [struct.unpack("<QQII", read_exact(source, 24)) for _ in range(count)]
            strings = read_exact(source, string_size)
            base_offset = source.tell()
            for offset, length, name_offset, _ in table:
                name = strings[name_offset:strings.index(b"\0", name_offset)].decode("ascii")
                source.seek(base_offset + offset)
                digest = hashlib.sha256()
                left = length
                while left:
                    data = read_exact(source, min(left, CHUNK))
                    digest.update(data)
                    left -= len(data)
                assert name.split(".")[0] == digest.hexdigest()[:32], name
                content.append((name, length, base_offset + offset))
            assert out.stat().st_size == base_offset + sum(row[1] for row in content)
            meta = next(row for row in content if row[0].endswith(".cnmt.nca"))
            source.seek(meta[2])
            meta_data = read_exact(source, meta[1])
        sys.path.insert(0, str(ROOT / "tests"))
        from validate_cnmt import meta_cnmt
        cnmt = meta_cnmt(meta_data)
        assert struct.unpack_from("<QIB", cnmt) == (0x0500A17E00070000, 0, 0x80)
        assert cnmt[-32:] == hashlib.sha256(cnmt[:-32]).digest()
        assert struct.unpack_from("<H", cnmt, 0x10)[0] == 2
        for index, row in enumerate(content[:2]):
            record = cnmt[0x30 + index * 0x38:0x30 + (index + 1) * 0x38]
            assert record[:16].hex() == row[0][:32]
            assert int.from_bytes(record[0x30:0x36], "little") == row[1]
        print(f"large_test PASS output_bytes={out.stat().st_size} "
              f"program_bytes={content[0][1]} peak_rss_kib={resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss} "
              f"elapsed_seconds={time.monotonic() - start:.1f}", flush=True)


if __name__ == "__main__":
    main()
