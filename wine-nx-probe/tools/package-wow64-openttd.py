#!/usr/bin/env python3
"""Add 32-bit OpenTTD 15.3 with OpenGFX 8.0 to the x86 Notepad package.

The inputs are the official portable builds, which are not stored in this
repository; WINE_NX_OPENTTD_INPUTS names the folder holding them (default
~/switch/winebox64_nx/local-inputs). Run package-wow64-notepad.py first."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile

probe = Path(__file__).resolve().parents[1]
pe = probe / 'build-wine-wow64-pe'
build = probe / 'build-switch-wow64-dynarec'
notepad = build / 'notepad-sd-card/switch/wine'
stage_root = build / 'openttd-sd-card'
stage = stage_root / 'switch/wine'
toolchain = probe / 'toolchains/llvm-mingw-20260505-ucrt-macos-universal/bin'
env = dict(os.environ, PATH=f'{toolchain}:/opt/homebrew/opt/bison/bin:' + os.environ['PATH'])
inputs = Path(os.environ.get('WINE_NX_OPENTTD_INPUTS', Path.home() / 'switch/winebox64_nx/local-inputs'))
openttd_zip = inputs / 'openttd-15.3-windows-win32.zip'
opengfx_zip = inputs / 'opengfx-8.0-all.zip'
OPENTTD_SHA256 = '3f092edc8f381c3d2d3a59458703899da6f876345b3850a3c76c0dffe68f0e74'
OPENGFX_SHA256 = '43a0c1dabf39cb865394f3a6cc36d4da5c10ecfaaf55652043104806810903be'
# GDI video without a drawing thread, no sound or music, the Switch's screen size,
# and the configuration next to the game.
ARGUMENTS = r'-v win32:no_threads -s null -m null -r 1280x720 -c C:\openttd\openttd.cfg'

for path, digest in ((openttd_zip, OPENTTD_SHA256), (opengfx_zip, OPENGFX_SHA256)):
    assert path.is_file(), f'Missing input: {path}'
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    assert actual == digest, f'{path.name} is not the tested package: {actual}'
assert (notepad / 'wine-nx-runtime.nro').is_file(), 'Run package-wow64-notepad.py first'

shutil.rmtree(stage_root, ignore_errors=True)
shutil.copytree(notepad, stage, ignore=shutil.ignore_patterns('*.log', '.DS_Store'))
game = stage / 'drive_c/openttd'
with ZipFile(openttd_zip) as z:
    for info in z.infolist():
        name = info.filename.split('/', 1)[1] if '/' in info.filename else ''
        if not name or info.is_dir():
            continue
        target = game / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.read(info))
with ZipFile(opengfx_zip) as z:
    with tarfile.open(fileobj=io.BytesIO(z.read('opengfx-8.0.tar'))) as tar:
        members = [m for m in tar.getmembers() if m.isfile() and '..' not in Path(m.name).parts]
        tar.extractall(game / 'baseset', members=members)
# The sprite font needs no FreeType fonts; declining the survey skips the modal
# question on the first start.
(game / 'openttd.cfg').write_text('[misc]\nprefer_sprite_font = true\n\n[network]\nparticipate_survey = no\n')
(game / 'openttd.args.txt').write_text(ARGUMENTS + '\n')

# The i386 import closure of openttd.exe, built from the PE tree.
queue, seen = [game / 'openttd.exe'], set()
syswow64 = stage / 'drive_c/windows/syswow64'
while queue:
    info = subprocess.check_output([str(toolchain / 'llvm-readobj'), '--coff-imports', str(queue.pop())], text=True)
    for name in re.findall(r'^Import \{\n  Name: (.+)$', info, re.M):
        name = name.lower()
        if name in seen:
            continue
        seen.add(name)
        module = name.removesuffix('.dll')
        assert re.fullmatch(r'[a-z0-9_-]+', module), name
        target = f'dlls/{module}/i386-windows/{name}'
        subprocess.run(['make', '-C', str(pe), '-j8', target], env=env, check=True)
        shutil.copy2(pe / target, syswow64 / name)
        queue.append(pe / target)

readme = (stage / 'README.txt').read_text()
(stage / 'README.txt').write_text(readme + '''
OpenTTD 15.3 (32-bit, with OpenGFX 8.0) is in C:\\openttd. Choose C:\\openttd\\openttd.exe
in the menu. openttd.args.txt next to it selects GDI video without a drawing thread, no
sound or music, a 1280x720 window and C:\\openttd\\openttd.cfg (sprite font). Networking
and OpenGL are unavailable: their DLLs load, but report every call as unsupported.
Expected first milestone: the OpenTTD main menu with the title game running behind it.
''')

subprocess.run([sys.executable, str(probe / 'tools/verify-wow64-package.py'), str(stage)], check=True)
exe_info = subprocess.check_output([str(toolchain / 'llvm-readobj'), '--coff-imports', str(game / 'openttd.exe')], text=True)
assert 'Arch: i386\n' in exe_info
imports = {n.lower() for n in re.findall(r'^Import \{\n  Name: (.+)$', exe_info, re.M)}
staged = {p.name.lower() for p in syswow64.glob('*.dll')}
assert imports <= staged, f'OpenTTD imports not staged: {imports - staged}'
assert list((game / 'baseset').rglob('opengfx.obg')), 'OpenGFX is missing'
assert (game / 'lang/english.lng').is_file()

archive = build / 'wine-nx-openttd-dynarec-17.zip'
with ZipFile(archive, 'w', ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file() and f.name != '.DS_Store' and f.suffix != '.log':
            z.write(f, f.relative_to(stage_root))
with ZipFile(archive) as z:
    assert z.testzip() is None
print(archive)
