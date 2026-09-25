"""Generate the local WA2 PAK map without editing any other source files.

The manifest contains game resource names, sizes, and hashes. Keep it local.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--manifest', type=Path, required=True)
ap.add_argument('--output', type=Path, required=True)
args = ap.parse_args()
rows = json.loads(args.manifest.read_text(encoding='utf-8'))
if not isinstance(rows, list) or not 1 <= len(rows) <= 128:
    raise SystemExit('manifest must contain 1..128 resources')
seen_paths, seen_pkg = set(), set()
for row in rows:
    if not isinstance(row, dict) or set(row) != {'path', 'pkg', 'size', 'sha256'}:
        raise SystemExit('resource must have path, pkg, size, sha256')
    path, pkg, size, sha = row['path'], row['pkg'], row['size'], row['sha256']
    if (not isinstance(path, str) or not path.isascii() or
        not all(re.fullmatch(r'[A-Za-z0-9_.-]+', part) for part in path.split('/')) or
        not path.lower().endswith('.pak') or len(path) > 160):
        raise SystemExit(f'unsafe resource path: {path!r}')
    if (not isinstance(pkg, str) or not re.fullmatch(r'wa2-asset-[0-9]{3}\.pak', pkg) or
        not isinstance(size, int) or not 0 <= size <= 0xffffffff or
        not isinstance(sha, str) or not re.fullmatch(r'[0-9a-f]{64}', sha)):
        raise SystemExit(f'invalid package metadata: {path!r}')
    if path.lower() in seen_paths or pkg in seen_pkg:
        raise SystemExit('duplicate game or package path')
    seen_paths.add(path.lower()); seen_pkg.add(pkg)

identity = hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
header = '''/* Generated from wa2-full-assets.json; immutable package assets only. */
#ifndef WINE_NX_PACKAGE_ASSETS_H
#define WINE_NX_PACKAGE_ASSETS_H
struct wa2_package_asset { const char *dos, *relative, *package; unsigned long long size; unsigned char sha[32]; };
static const struct wa2_package_asset wa2_package_assets[] = {\n'''
for row in rows:
    header += '    {' + ','.join([
        json.dumps('\\wa2\\' + row['path'].lower().replace('/', '\\')),
        json.dumps('drive_c/WA2/' + row['path']),
        json.dumps('wapkg:/' + row['pkg']),
        str(row['size']) + 'ULL',
        '{' + ','.join('0x' + row['sha256'][i:i+2] for i in range(0, 64, 2)) + '}'
    ]) + '},\n'
header += '};\n#define WA2_PACKAGE_COUNT (sizeof(wa2_package_assets)/sizeof(wa2_package_assets[0]))\n'
header += '#define WA2_PACKAGE_ID "' + identity + '"\n#endif\n'
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(header, encoding='utf-8')
print(f'generated {len(rows)} mappings; manifest identity {identity}')
