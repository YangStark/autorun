#!/usr/bin/env python3
"""One zip with everything a card needs: the whole payload, DXVK over it.

package-wow64-full.py stages Wine's files, the i386 DLLs the staged programs
import, the test programs and the game setups, and writes them as one archive.
package-wow64-dxvk.py builds DXVK's d3d9.dll and Wine's Vulkan DLLs for i386 as
an overlay, for a card that already holds that payload.

Someone installing Autorun wants neither half by itself, so this runs both and
writes what they come to as autorun-NNN.zip: unzip it at the root of the card
and everything is in switch/wine. The two halves' own archives are taken away
afterwards, so the build folder holds the one zip.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import re
import subprocess
import sys

probe = Path(__file__).resolve().parents[1]
tools = probe / 'tools'
build = probe / 'build-switch-wow64-dynarec'
stage_root = build / 'full-sd-card'
stage = stage_root / 'switch/wine'
marker = re.search(r'nx-wow64-dynarec-(\d+)', (probe / 'source/runtime.c').read_text()).group(1)

subprocess.run([sys.executable, str(tools / 'package-wow64-full.py')], check=True)
subprocess.run([sys.executable, str(tools / 'package-wow64-dxvk.py')], check=True)

full = build / f'wine-nx-full-dynarec-{marker}.zip'
overlay = build / f'wine-nx-dxvk-overlay-dynarec-{marker}.zip'
assert full.is_file() and overlay.is_file(), 'a half is missing'

# The overlay's paths are the card's own, so it unpacks onto the staged payload
# the way it would onto the card: a newer runtime and the DXVK files.
with ZipFile(overlay) as z:
    for name in z.namelist():
        assert name.startswith('switch/wine/'), name
    z.extractall(stage_root)

subprocess.run([sys.executable, str(tools / 'verify-wow64-package.py'), str(stage)], check=True)

archive = build / f'autorun-{marker}.zip'
with ZipFile(archive, 'w', ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file() and f.name != '.DS_Store' and f.suffix != '.log':
            z.write(f, f.relative_to(stage_root))
        # Empty folders are places to copy a game into, such as drive_c/WarCraft III.
        elif f.is_dir() and not any(f.iterdir()):
            z.write(f, f.relative_to(stage_root))
with ZipFile(archive) as z:
    assert z.testzip() is None
    files = len(z.infolist())

full.unlink()
overlay.unlink()
print(f'{archive} ({archive.stat().st_size / 2**20:.1f} MiB, {files} files)')
