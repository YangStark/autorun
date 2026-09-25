#!/usr/bin/env python3
"""Stream a fixed-root, immutable runtime payload into the v2 resource bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

GAME_ID = "autorun-games/wa2-full"
MAX_FILES = 16384
MAX_SIZE = 32 * 1024 * 1024 * 1024
MAX_FILE_SIZE = 0xffffffff
CHUNK = 65536
REQUIRED = {"wine-nx-runtime.nro", "drive_c/WA2/WA2_full_menu.exe"}
SEGMENT = re.compile(r"[A-Za-z0-9_.-]+\Z")
STATE_NAMES = {"ready.sha256", "user.reg", "system.reg", "userdef.reg", "verify-all.flag"}
STATE_SUFFIXES = (".part", ".log", ".sav", ".save", ".dmp")


def canonical(path: str) -> bool:
    parts = path.split("/")
    return (0 < len(path.encode("ascii", errors="ignore")) <= 191
            and len(path.encode("ascii", errors="ignore")) == len(path)
            and all(p not in ("", ".", "..") and SEGMENT.fullmatch(p) for p in parts)
            and path.lower() not in ("ready.sha256", "verify-all.flag")
            and not path.lower().endswith(".part"))


def layout_sizes(sizes):
    if not 1 <= len(sizes) <= MAX_FILES:
        raise ValueError(f"file count must be 1..{MAX_FILES}")
    offset = 48 + 240 * len(sizes)
    offsets = []
    for size in sizes:
        if size < 0 or size > MAX_FILE_SIZE:
            raise ValueError("file exceeds FAT32 limit")
        offsets.append(offset)
        offset += size
        if offset > MAX_SIZE:
            raise ValueError("bundle exceeds 32 GiB")
    return offsets, offset


def bundle(payload: Path, output: Path, manifest: Path) -> None:
    if not payload.is_dir() or payload.is_symlink():
        raise ValueError("payload must be a real directory")
    files = []
    for path in payload.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"symlink rejected: {path}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise ValueError(f"special file rejected: {path}")
        name = path.relative_to(payload).as_posix()
        if not canonical(name):
            raise ValueError(f"unsafe bundle path: {name}")
        low = name.lower()
        if low.endswith(".pak"): raise ValueError("PAK files are reserved for Program RomFS")
        if (low == "drive_c/wa2/script.pak"
                or path.name.lower() in STATE_NAMES or low.endswith(STATE_SUFFIXES)
                or low == "drive_c/wa2/wa2_full_menu.wine-nx.txt"
                or low.startswith(("registry/", "logs/", "drive_c/users/"))
                or (low.startswith("config/") and low != "config/classes.reg")) :
            raise ValueError(f"mutable state rejected: {name}")
        size = path.stat().st_size
        if size > MAX_FILE_SIZE:
            raise ValueError(f"file exceeds FAT32 limit: {name}")
        files.append((name, path, size))
    files.sort(key=lambda item: (item[0] != "wine-nx-runtime.nro", item[0].lower()))
    names = {name for name, _, _ in files}
    if not REQUIRED <= names:
        raise ValueError(f"missing required payload files: {sorted(REQUIRED - names)}")
    if not 1 <= len(files) <= MAX_FILES:
        raise ValueError(f"file count must be 1..{MAX_FILES}")
    seen = set()
    folded = {name.lower() for name, _, _ in files}
    for name, _, _ in files:
        low = name.lower()
        if low in seen or any('/'.join(low.split('/')[:i]) in folded
                              for i in range(1, len(low.split('/')))):
            raise ValueError(f"case-insensitive path collision: {name}")
        seen.add(low)
    offsets, total = layout_sizes([size for _, _, size in files])
    rows = []
    for (name, path, size), offset in zip(files, offsets):
        h = hashlib.sha256()
        with path.open("rb") as source:
            while chunk := source.read(CHUNK):
                h.update(chunk)
        rows.append((name, path, size, offset, h.digest()))
    output.parent.mkdir(parents=True, exist_ok=True)
    # A failed build must not replace the previous usable bundle.
    temporary = output.with_name(output.name + ".tmp")
    if output.resolve().is_relative_to(payload.resolve()) or manifest.resolve().is_relative_to(payload.resolve()):
        raise ValueError("output/manifest must be outside payload")
    with temporary.open("wb") as dest:
        dest.write(struct.pack("<8sII32s", b"ARPBNDL2", 2, len(rows), GAME_ID.encode()))
        for name, _, size, offset, digest in rows:
            dest.write(struct.pack("<192sQQ32s", name.encode(), offset, size, digest))
        for name, path, size, _, digest in rows:
            h = hashlib.sha256()
            copied = 0
            with path.open("rb") as source:
                while chunk := source.read(CHUNK):
                    dest.write(chunk)
                    h.update(chunk)
                    copied += len(chunk)
            if copied != size or h.digest() != digest:
                raise ValueError(f"payload changed while packing: {name}")
    temporary.replace(output)
    report = {"schema": 2, "game_id": GAME_ID,
              "deploy_root": "sdmc:/switch/" + GAME_ID,
              "bundle_size": total,
              "files": [{"path": n, "size": s, "sha256": d.hex()} for n, _, s, _, d in rows]}
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"bundle: {len(rows)} files, {total} bytes")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--payload", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    args = ap.parse_args()
    bundle(args.payload, args.output, args.manifest)
