#!/usr/bin/env python3
"""Bounded-memory validation of the full WA2 direct-asset NSP."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT / "wa2-complete-nsp"
sys.path.insert(0, str(PROJECT / "tests"))
from validate_cnmt import validate as validate_cnmt

TITLE = 0x0500A17E00070000
GAME_ID = b"autorun-games/wa2-full"
CHUNK = 1024 * 1024


def exact(source, count):
    data = source.read(count)
    if len(data) != count:
        raise ValueError("truncated package")
    return data


def digest_range(source, start, size):
    source.seek(start)
    digest = hashlib.sha256()
    while size:
        block = exact(source, min(size, CHUNK))
        digest.update(block)
        size -= len(block)
    return digest.hexdigest()


def pfs0(source, start, extent, max_count=64):
    source.seek(start)
    magic, count, strings_size, _ = struct.unpack("<4sIII", exact(source, 16))
    if magic != b"PFS0" or count > max_count or strings_size > 65536:
        raise ValueError("invalid PFS0 header")
    table = exact(source, 24 * count)
    strings = exact(source, strings_size)
    data_start = 16 + 24 * count + strings_size
    if data_start > extent:
        raise ValueError("PFS0 table exceeds section")
    found = {}
    for index in range(count):
        offset, size, name_offset, _ = struct.unpack_from("<QQII", table, 24 * index)
        if name_offset >= strings_size:
            raise ValueError("PFS0 name offset exceeds table")
        end = strings.find(b"\0", name_offset)
        if end < 0:
            raise ValueError("unterminated PFS0 name")
        name = strings[name_offset:end].decode("ascii")
        if name in found or offset > extent - data_start or size > extent - data_start - offset:
            raise ValueError("duplicate or out-of-bounds PFS0 entry")
        found[name] = (start + data_start + offset, size)
    return found


def find_exefs(source, program_start, program_size):
    limit = min(program_size, 16 * 1024 * 1024)
    source.seek(program_start + 0xC00)
    head = exact(source, limit - 0xC00)
    at = head.find(b"PFS0")
    while at >= 0:
        candidate = program_start + 0xC00 + at
        try:
            files = pfs0(source, candidate, program_size - (candidate - program_start))
            if "main.npdm" in files and "main" in files:
                return files
        except (ValueError, struct.error, UnicodeDecodeError):
            pass
        at = head.find(b"PFS0", at + 4)
    raise ValueError("Program ExeFS not found in bounded header region")


def find_romfs(source, program_start, program_size, expected_names):
    limit = min(program_size, 32 * 1024 * 1024)
    source.seek(program_start + 0xC00)
    head = exact(source, limit - 0xC00)
    needle = struct.pack("<Q", 80)
    at = head.find(needle)
    while at >= 0:
        relative = 0xC00 + at
        if at + 80 <= len(head):
            h = struct.unpack_from("<10Q", head, at)
            # Header: size, dir hash/table offsets and sizes, file hash/table,
            # then the file data offset. Bound every table before reading it.
            if (h[0] == 80 and h[9] == 0x200 and h[6] <= 4096 and
                0 < h[8] <= 65536 and h[7] <= program_size - relative and
                h[8] <= program_size - relative - h[7]):
                source.seek(program_start + relative + h[7])
                table = exact(source, h[8])
                files = {}
                offset = 0
                try:
                    while offset < len(table):
                        if len(table) - offset < 32:
                            raise ValueError("short RomFS file entry")
                        parent, sibling, dataoff, size, chain, namelen = struct.unpack_from("<IIQQII", table, offset)
                        end = offset + 32 + ((namelen + 3) & ~3)
                        if namelen > 191 or end > len(table) or parent != 0:
                            raise ValueError("invalid RomFS file entry")
                        name = table[offset + 32:offset + 32 + namelen].decode("ascii")
                        if name in files or h[9] + dataoff > program_size - relative or size > program_size - relative - h[9] - dataoff:
                            raise ValueError("duplicate or out-of-bounds RomFS entry")
                        files[name] = (program_start + relative + h[9] + dataoff, size)
                        offset = end
                    if set(files) == expected_names:
                        return files
                except (ValueError, UnicodeDecodeError, struct.error):
                    pass
        at = head.find(needle, at + 8)
    raise ValueError("Program RomFS table not found in bounded header region")


def validate_npdm(source, extent):
    start, size = extent
    if size > 1024 * 1024:
        raise ValueError("NPDM exceeds validation bound")
    source.seek(start)
    body = exact(source, size)
    aci, _, acid, _ = struct.unpack_from("<IIII", body, 0x70)
    if struct.unpack_from("<Q", body, aci + 0x10)[0] != TITLE:
        raise ValueError("NPDM title mismatch")
    if (body[12] >> 1) & 7 != 2:
        raise ValueError("NPDM address space is not 32-bit no-alias")
    for base, where in ((aci, 0x30), (acid, 0x230)):
        off, length = struct.unpack_from("<II", body, base + where)
        words = struct.unpack_from("<" + "I" * (length // 4), body, base + off)
        if [word for word in words if word & 0x1ffff == 0xffff] != [0x4ffff]:
            raise ValueError("NPDM memory limit mismatch")


def validate_bundle(source, extent, manifest):
    start, size = extent
    source.seek(start)
    magic, version, count, game = struct.unpack("<8sII32s", exact(source, 48))
    if (magic, version, game.rstrip(b"\0")) != (b"ARPBNDL2", 2, GAME_ID):
        raise ValueError("bundle identity mismatch")
    expected = {row["path"].lower(): row for row in manifest["files"]}
    if count != len(expected) or count > 16384:
        raise ValueError("bundle file count mismatch")
    table = exact(source, count * 240)
    seen, next_offset = set(), 48 + 240 * count
    for index in range(count):
        raw_name, offset, length, digest = struct.unpack_from("<192sQQ32s", table, 240 * index)
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        low = name.lower()
        if (low not in expected or low in seen or low.endswith(".pak") or
            offset != next_offset or length > 0xffffffff or length > size - offset):
            raise ValueError(f"invalid bundle entry {index}: {name}")
        row = expected[low]
        if length != row["size"] or digest.hex() != row["sha256"]:
            raise ValueError(f"bundle manifest mismatch: {name}")
        if digest_range(source, start + offset, length) != row["sha256"]:
            raise ValueError(f"bundle content hash mismatch: {name}")
        seen.add(low)
        next_offset += length
    if seen != set(expected) or next_offset != size:
        raise ValueError("bundle extents or membership mismatch")
    return count


def validate(path, assets_path, bundle_manifest_path, cnmt_already_validated=False):
    assets = json.loads(assets_path.read_text(encoding="utf-8"))
    bundle_manifest = json.loads(bundle_manifest_path.read_text(encoding="utf-8"))
    if len(assets) != 49:
        raise ValueError("expected 49 assets")
    if not cnmt_already_validated:
        validate_cnmt(path)
    with path.open("rb") as source:
        ncas = pfs0(source, 0, path.stat().st_size, 3)
        programs = [row for name, row in ncas.items() if not name.endswith(".cnmt.nca") and row[1] > 100 * 1024 * 1024]
        if len(programs) != 1:
            raise ValueError("expected one large Program NCA")
        program_start, program_size = programs[0]
        controls = [row for name, row in ncas.items() if not name.endswith(".cnmt.nca") and row != programs[0]]
        if len(controls) != 1:
            raise ValueError("expected one Control NCA")
        control_files = find_romfs(source, *controls[0], {"control.nacp", "icon_AmericanEnglish.dat"})
        nacp_start, nacp_size = control_files["control.nacp"]
        source.seek(nacp_start)
        nacp = exact(source, nacp_size)
        if nacp[:0x200].split(b"\0", 1)[0] != b"White Album 2" or b"0.3.1\0" not in nacp:
            raise ValueError("wrong HOME title or display version")
        icon_start, icon_size = control_files["icon_AmericanEnglish.dat"]
        expected_icon = (PROJECT / "assets/icon-white.jpg").read_bytes()
        source.seek(icon_start)
        if exact(source, icon_size) != expected_icon:
            raise ValueError("wrong HOME icon")
        exefs = find_exefs(source, program_start, program_size)
        validate_npdm(source, exefs["main.npdm"])
        names = {"nextArgv", "nextNroPath", "bundle.bin"} | {row["pkg"] for row in assets}
        files = find_romfs(source, program_start, program_size, names)
        expected_root = b"sdmc:/switch/autorun-games/wa2-full"
        for name, value in (("nextNroPath", expected_root + b"/wine-nx-runtime.nro"),
                            ("nextArgv", expected_root + b"/wine-nx-runtime.nro " + expected_root + b"/drive_c/WA2/WA2_full_menu.exe")):
            start, size = files[name]
            source.seek(start)
            if exact(source, size) != value:
                raise ValueError(f"wrong {name}")
        bundle_count = validate_bundle(source, files["bundle.bin"], bundle_manifest)
        for row in assets:
            start, size = files[row["pkg"]]
            if size != row["size"] or digest_range(source, start, size) != row["sha256"]:
                raise ValueError(f"asset mismatch: {row['path']}")
        source.seek(0)
        nsp_sha256 = digest_range(source, 0, path.stat().st_size)
    return {"result": "PASS", "title": f"{TITLE:016X}", "version": "0.3.1",
            "nsp_bytes": path.stat().st_size, "romfs_files": len(files),
            "bundle_files": bundle_count, "direct_assets": len(assets),
            "direct_asset_bytes": sum(row["size"] for row in assets),
            "nsp_sha256": nsp_sha256,
            "cnmt": "packager validated" if cnmt_already_validated else "PASS",
            "npdm": "PASS", "home_name": "White Album 2", "home_icon": "PASS",
            "hardware_verified": False}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--nsp", type=Path, default=ROOT.parent / "outputs/White-Album-2-0.3.1/White-Album-2-0.3.1.nsp")
    ap.add_argument("--assets-manifest", type=Path, default=ROOT / "wa2-full-assets.json")
    ap.add_argument("--bundle-manifest", type=Path, default=PROJECT / "build/payload-manifest.json")
    ap.add_argument("--report", type=Path, default=ROOT.parent / "outputs/White-Album-2-0.3.1/validation.json")
    ap.add_argument("--cnmt-already-validated", action="store_true",
                    help="trust the packager's streaming CNMT check and skip repeating it")
    args = ap.parse_args()
    report = validate(args.nsp, args.assets_manifest, args.bundle_manifest,
                      args.cnmt_already_validated)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
