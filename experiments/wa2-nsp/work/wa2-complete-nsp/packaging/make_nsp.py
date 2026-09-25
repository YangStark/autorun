#!/usr/bin/env python3
"""Build a rights-free homebrew NSP from locally built loader assets.

This only accepts caller-provided files and a locally supplied NCA header key.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

TITLE_ID = "0500A17E00070000"
NRO_PATH = "sdmc:/switch/autorun-games/wa2-full/wine-nx-runtime.nro"
MAX_ASSETS = 128
MAX_BUNDLE = 32 * 1024 * 1024 * 1024
MAX_FILE = 0xffffffff


def validate_bundle_bounds(path: Path) -> None:
    size = path.stat().st_size
    if size > MAX_BUNDLE or size < 48:
        raise ValueError("runtime bundle size must be 48 bytes to 32 GiB")
    with path.open("rb") as source:
        magic, version, count, _ = struct.unpack("<8sII32s", source.read(48))
        if magic != b"ARPBNDL2" or version != 2 or not 1 <= count <= 16384:
            raise ValueError("invalid v2 runtime bundle header")
        expected = 48 + 240 * count
        if expected > size:
            raise ValueError("truncated runtime bundle table")
        names = set()
        for index in range(count):
            name, offset, length, _ = struct.unpack("<192sQQ32s", source.read(240))
            decoded = name.split(b"\0", 1)[0].decode("ascii")
            if not decoded or decoded.lower().endswith(".pak"):
                raise ValueError("PAK files are reserved for Program RomFS")
            if decoded.lower() in names:
                raise ValueError("duplicate bundle path")
            names.add(decoded.lower())
            if index == 0 and name.split(b"\0", 1)[0] != b"wine-nx-runtime.nro":
                raise ValueError("runtime NRO must be the first bundle entry")
            if length > MAX_FILE:
                raise ValueError(f"bundle entry {index} exceeds FAT32 file bound")
            if offset != expected or length > size - offset:
                raise ValueError(f"bundle entry {index} has invalid 64-bit extent")
            expected += length
        if expected != size:
            raise ValueError("bundle data length does not match table")


def read_assets(manifest: Path, root: Path) -> list[tuple[str, Path, int, str]]:
    rows = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(rows, list) or not 1 <= len(rows) <= MAX_ASSETS:
        raise ValueError("asset count must be 1..128")
    seen_paths, seen_packages, result = set(), set(), []
    root = root.resolve(strict=True)
    for row in rows:
        if not isinstance(row, dict) or set(row) != {"path", "pkg", "size", "sha256"}:
            raise ValueError("invalid asset manifest row")
        name, pkg, size, digest = (row[key] for key in ("path", "pkg", "size", "sha256"))
        def canonical(value: str) -> bool:
            return (isinstance(value, str) and 0 < len(value) <= 191 and
                    all(re.fullmatch(r"[A-Za-z0-9_.-]+", part) and part not in (".", "..")
                        for part in value.split("/")))
        if not canonical(name) or not canonical(pkg) or "/" in pkg or not pkg.lower().endswith(".pak"):
            raise ValueError("unsafe asset path or package name")
        if name.lower() in seen_paths or pkg.lower() in seen_packages:
            raise ValueError("case-insensitive asset collision")
        seen_paths.add(name.lower()); seen_packages.add(pkg.lower())
        if not isinstance(size, int) or not 0 < size <= MAX_FILE or not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("invalid asset size or SHA-256")
        path = (root / name).resolve(strict=True)
        if not path.is_relative_to(root) or not path.is_file() or path.is_symlink() or path.stat().st_size != size:
            raise ValueError(f"missing or changed asset: {name}")
        result.append((pkg, path, size, digest))
    return result


def write_asset_header(path: Path, assets: list[tuple[str, Path, int, str]]) -> None:
    names = ",\n".join(f'    "/{pkg}"' for pkg, _, _, _ in assets)
    content = f"/* Generated from the ordered asset manifest. */\n#define PROGRAM_ASSET_COUNT {len(assets)}\nstatic const char *const program_asset_names[PROGRAM_ASSET_COUNT] = {{\n{names}\n}};\n"
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def pfs0_header(entries: list[tuple[str, Path]]) -> bytes:
    names = b"".join(name.encode("ascii") + b"\0" for name, _ in entries)
    strings = names + bytes((-len(names)) % 0x20)
    table = bytearray()
    data_offset = name_offset = 0
    for name, path in entries:
        size = path.stat().st_size
        table += struct.pack("<QQII", data_offset, size, name_offset, 0)
        data_offset += size
        name_offset += len(name) + 1
    return struct.pack("<4sIII", b"PFS0", len(entries), len(strings), 0) + table + strings


def sha256_file(path: Path) -> bytes:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.digest()


def write_nsp(path: Path, entries: list[tuple[str, Path]]) -> int:
    header = pfs0_header(entries)
    total = len(header)
    with path.open("wb") as target:
        target.write(header)
        for _, source_path in entries:
            with source_path.open("rb") as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    target.write(chunk)
                    total += len(chunk)
    return total


def preflight_space(output_dir: Path, bundle_size: int) -> None:
    # Peak live data: temporary RomFS, final NCAs, and temporary NSP. Allow
    # 64 MiB per copy for integrity layers, alignment, and small NCAs.
    copy_budget = bundle_size + 64 * 1024 * 1024
    temp_dir = Path(tempfile.gettempdir())
    same_device = output_dir.stat().st_dev == temp_dir.stat().st_dev
    output_need = copy_budget * (3 if same_device else 2)
    if shutil.disk_usage(output_dir).free < output_need:
        raise ValueError(f"insufficient free space for packaging: need about {output_need // (1024 ** 3) + 1} GiB on output filesystem")
    if not same_device and shutil.disk_usage(temp_dir).free < copy_budget:
        raise ValueError("insufficient free space for temporary RomFS on temp filesystem")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--main", required=True, type=Path, help="compiled loader main NSO")
    ap.add_argument("--npdm", required=True, type=Path, help="loader main.npdm")
    ap.add_argument("--icon", required=True, type=Path, help="256x256 baseline JPEG")
    ap.add_argument("--bundle", required=True, type=Path, help="runtime bundle.bin")
    ap.add_argument("--assets-manifest", required=True, type=Path)
    ap.add_argument("--asset-root", required=True, type=Path)
    ap.add_argument("--header-key", required=True, type=Path, help="local 32-byte header key file")
    ap.add_argument("--output", required=True, type=Path, help="resulting .nsp")
    ap.add_argument("--packager", type=Path, default=Path(__file__).with_name("packager"))
    ap.add_argument("--atmosphere", required=True, help="target Atmosphere version, e.g. 1.6.1")
    args = ap.parse_args()
    if not re.fullmatch(r"[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}", args.atmosphere) or any(int(n)>255 for n in args.atmosphere.split(".")):
        ap.error("invalid target Atmosphere version")
    try:
        assets = read_assets(args.assets_manifest, args.asset_root)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        ap.error(str(exc))
    for path in (args.main, args.npdm, args.icon, args.bundle, args.header_key):
        if not path.is_file():
            ap.error(f"missing input: {path}")
    asset_size = sum(row[2] for row in assets)
    try:
        validate_bundle_bounds(args.bundle)
    except ValueError as exc:
        ap.error(str(exc))

    write_asset_header(Path(__file__).with_name("asset_names.h"), assets)
    sources = [Path(__file__).with_name(name) for name in
               ("packager.c", "stream_packager.c", "forwarder.c", "forwarder.h", "switch.h")]
    sources.append(Path(__file__).with_name("asset_names.h"))
    if (not args.packager.is_file() or
            any(path.stat().st_mtime > args.packager.stat().st_mtime for path in sources)):
        subprocess.run(["gcc", "-std=gnu11", "-O2", "-I", str(Path(__file__).parent),
                        str(sources[0]), "/lib/x86_64-linux-gnu/libcrypto.so.3",
                        "-o", str(args.packager)], check=True)

    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        preflight_space(out.parent, args.bundle.stat().st_size + asset_size)
    except ValueError as exc:
        ap.error(str(exc))
    nca_dir = out.parent / (out.stem + "-nca")
    nca_dir.mkdir(exist_ok=True)
    cmd = [str(args.packager.resolve()), str(args.main.resolve()), str(args.npdm.resolve()),
           str(args.icon.resolve()), str(args.bundle.resolve()), str(args.header_key.resolve()),
           str(nca_dir), NRO_PATH, "White Album 2", "YangStark", args.atmosphere,
           *[str(row[1]) for row in assets]]
    subprocess.run(cmd, check=True)

    ncas = [nca_dir / f"nca-{i}.bin" for i in (1, 2, 3)]
    if not all(path.is_file() for path in ncas):
        raise RuntimeError("packager did not produce program, control, and meta NCAs")
    names = [sha256_file(path)[:16].hex() + ".nca" for path in ncas]
    names[2] = names[2][:-4] + ".cnmt.nca"
    entries = list(zip(names, ncas))
    tmp = out.with_name(out.name + ".tmp")
    total = write_nsp(tmp, entries)
    validator = Path(__file__).resolve().parents[1] / "tests" / "validate_cnmt.py"
    subprocess.run([sys.executable, str(validator), str(tmp)], check=True)
    os.replace(tmp, out)
    print(f"{out}: {total} bytes, title {TITLE_ID}, 3 NCAs")


if __name__ == "__main__":
    main()
