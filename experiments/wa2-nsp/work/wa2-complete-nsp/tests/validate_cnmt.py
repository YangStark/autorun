#!/usr/bin/env python3
"""Check the packaged CNMT against the NCAs in a prototype NSP."""

import argparse
import hashlib
from pathlib import Path
import struct
import sys

TITLE_ID = 0x0500A17E00070000
RECORD_SIZE = 0x38
MAX_META_NCA = 16 * 1024 * 1024
CHUNK = 1024 * 1024


def pfs0_files(data: bytes, offset: int = 0) -> dict[str, bytes]:
    if data[offset:offset + 4] != b"PFS0" or len(data) - offset < 0x10:
        raise ValueError("PFS0 header missing")
    count, string_size = struct.unpack_from("<II", data, offset + 4)
    if count > 64 or string_size > 0x10000:
        raise ValueError("implausible PFS0 table")
    table = offset + 0x10
    names = table + count * 0x18
    base = names + string_size
    if base > len(data):
        raise ValueError("PFS0 table exceeds file")
    result = {}
    for index in range(count):
        entry = table + index * 0x18
        file_offset, size, name_offset, _ = struct.unpack_from("<QQII", data, entry)
        if name_offset >= string_size:
            raise ValueError("PFS0 name offset exceeds string table")
        name_end = data.find(b"\0", names + name_offset, names + string_size)
        if name_end < 0:
            raise ValueError("unterminated PFS0 name")
        name = data[names + name_offset:name_end].decode("ascii")
        if file_offset > len(data) - base or size > len(data) - base - file_offset:
            raise ValueError(f"PFS0 file {name} exceeds container")
        if name in result:
            raise ValueError(f"duplicate PFS0 file {name}")
        result[name] = data[base + file_offset:base + file_offset + size]
    return result


def meta_cnmt(nca: bytes) -> bytes:
    # The NCA header is XTS encrypted; its PFS0 section is plaintext here.
    position = nca.find(b"PFS0", 0xC00)
    while position >= 0:
        try:
            files = pfs0_files(nca, position)
            found = [body for name, body in files.items()
                     if name.startswith("Application_") and name.endswith(".cnmt")]
            if len(found) == 1:
                return found[0]
        except (ValueError, struct.error, UnicodeDecodeError):
            pass
        position = nca.find(b"PFS0", position + 4)
    raise ValueError("meta NCA has no readable Application CNMT")


def validate(path: Path) -> None:
    file_size = path.stat().st_size
    with path.open("rb") as source:
        header = source.read(16)
        if len(header) != 16 or header[:4] != b"PFS0":
            raise ValueError("PFS0 header missing")
        count, string_size = struct.unpack_from("<II", header, 4)
        if count != 3 or string_size > 0x10000:
            raise ValueError("expected exactly three NCA files in the NSP")
        table_size = count * 0x18 + string_size
        if 16 + table_size > file_size:
            raise ValueError("PFS0 table exceeds file")
        table = source.read(table_size)
        if len(table) != table_size:
            raise ValueError("truncated PFS0 table")
        strings = table[count * 0x18:]
        base = 16 + table_size
        entries = []
        for index in range(count):
            offset, size, name_offset, _ = struct.unpack_from("<QQII", table, index * 0x18)
            if name_offset >= string_size:
                raise ValueError("PFS0 name offset exceeds string table")
            name_end = strings.find(b"\0", name_offset)
            if name_end < 0:
                raise ValueError("unterminated PFS0 name")
            name = strings[name_offset:name_end].decode("ascii")
            if offset > file_size - base or size > file_size - base - offset:
                raise ValueError(f"PFS0 file {name} exceeds container")
            entries.append((name, offset, size))
        if len({name for name, _, _ in entries}) != 3 or any(not name.endswith(".nca") for name, _, _ in entries):
            raise ValueError("expected exactly three NCA files in the NSP")
        expected_offset = 0
        for _, offset, size in entries:
            if offset != expected_offset:
                raise ValueError("NSP NCA offsets are not contiguous")
            expected_offset += size
        if base + sum(size for _, _, size in entries) != file_size:
            raise ValueError("NSP contains trailing data")
        ncas = {}
        meta_body = None
        for name, offset, size in entries:
            source.seek(base + offset)
            digest = hashlib.sha256()
            body = bytearray() if name.endswith(".cnmt.nca") else None
            if body is not None and size > MAX_META_NCA:
                raise ValueError("meta NCA exceeds 16 MiB validation bound")
            left = size
            while left:
                chunk = source.read(min(left, CHUNK))
                if not chunk:
                    raise ValueError(f"truncated NCA: {name}")
                digest.update(chunk)
                if body is not None:
                    body.extend(chunk)
                left -= len(chunk)
            file_hash = digest.digest()
            if name.split(".")[0] != file_hash.hex()[:32]:
                raise ValueError(f"NCA filename does not match content hash: {name}")
            ncas[name] = (file_hash, size)
            if body is not None:
                if meta_body is not None:
                    raise ValueError("expected one .cnmt.nca")
                meta_body = bytes(body)
    if meta_body is None:
        raise ValueError("expected one .cnmt.nca")
    cnmt = meta_cnmt(meta_body)
    if len(cnmt) < 0x30 + 0x20:
        raise ValueError("CNMT is truncated")
    title_id, version, meta_type = struct.unpack_from("<QIB", cnmt)
    extended_size, content_count, meta_count = struct.unpack_from("<HHH", cnmt, 0xE)
    if (title_id, version, meta_type) != (TITLE_ID, 0, 0x80):
        raise ValueError("unexpected CNMT title, version, or application type")
    if extended_size != 0x10 or content_count != 2 or meta_count != 0:
        raise ValueError(f"invalid standalone application CNMT counts: "
                         f"extended={extended_size}, content={content_count}, meta={meta_count}")
    if cnmt[0x15] != 0:
        raise ValueError("packaged CNMT storage field must be zero")
    expected_length = 0x20 + extended_size + content_count * RECORD_SIZE + meta_count * 0x10 + 0x20
    if len(cnmt) != expected_length:
        raise ValueError(f"CNMT length {len(cnmt):#x} != declared length {expected_length:#x}")
    if cnmt[-0x20:] != hashlib.sha256(cnmt[:-0x20]).digest():
        raise ValueError("CNMT trailing digest does not match its contents")
    patch_id = struct.unpack_from("<Q", cnmt, 0x20)[0]
    if patch_id != TITLE_ID | 0x800:
        raise ValueError("CNMT patch ID does not match application")
    remaining = {digest: size for name, (digest, size) in ncas.items()
                 if not name.endswith(".cnmt.nca")}
    types = set()
    for index in range(content_count):
        record = cnmt[0x20 + extended_size + index * RECORD_SIZE:
                      0x20 + extended_size + (index + 1) * RECORD_SIZE]
        digest = record[:0x20]
        if digest not in remaining:
            raise ValueError(f"CNMT content {index} hash not found in NSP")
        content_size = remaining.pop(digest)
        content_id = record[0x20:0x30]
        size = int.from_bytes(record[0x30:0x36], "little")
        kind = record[0x36]
        if content_id != digest[:0x10] or size != content_size or record[0x37] != 0:
            raise ValueError(f"CNMT content {index} ID, size, or ID offset mismatch")
        types.add(kind)
    if remaining or types != {1, 3}:
        raise ValueError("CNMT must reference the program and control NCAs exactly once")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nsp", type=Path)
    args = parser.parse_args()
    try:
        validate(args.nsp)
    except (OSError, ValueError, UnicodeDecodeError, struct.error) as exc:
        print(f"FAIL: {args.nsp}: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: {args.nsp}: CNMT structure, digest, and NCA references")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
