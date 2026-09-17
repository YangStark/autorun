#!/usr/bin/env python3
"""Add 32-bit OpenTTD 15.3 with OpenGFX 8.0 to the x86 Notepad package.

The inputs are the official portable builds, which are not stored in this
repository; WINE_NX_OPENTTD_INPUTS names the folder holding them (default
~/switch/winebox64_nx/local-inputs). Run package-wow64-notepad.py first."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import functools
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
opensfx_zip = inputs / 'opensfx-1.0.3-all.zip'
openmsx_zip = inputs / 'openmsx-0.3.1-all.zip'
OPENTTD_SHA256 = '3f092edc8f381c3d2d3a59458703899da6f876345b3850a3c76c0dffe68f0e74'
OPENGFX_SHA256 = '43a0c1dabf39cb865394f3a6cc36d4da5c10ecfaaf55652043104806810903be'
OPENSFX_SHA256 = 'e0a218b7dd9438e701503b0f84c25a97c1c11b7c2f025323fb19d6db16ef3759'
OPENMSX_SHA256 = '92e293ae89f13ad679f43185e83fb81fb8cad47fe63f4af3d3d9f955130460f5'
# OpenGL video (Mesa on the Switch GPU) without a drawing thread, sound effects through the win32
# (winmm) sound driver, no music, the Switch's screen size, and the configuration next to the game.
# -v, -s and -m name drivers; OpenTTD picks the OpenSFX base set it finds by itself. Changing the
# video driver to win32 in openttd.args.txt goes back to the GDI path.
ARGUMENTS = r'-v win32-opengl:no_threads -s win32 -m null -r 1280x720 -c C:\openttd\openttd.cfg'

for path, digest in ((openttd_zip, OPENTTD_SHA256), (opengfx_zip, OPENGFX_SHA256), (opensfx_zip, OPENSFX_SHA256), (openmsx_zip, OPENMSX_SHA256)):
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
for package in (opensfx_zip, openmsx_zip):
    with ZipFile(package) as z:
        for member in z.namelist():
            if not member.endswith('.tar'):
                continue
            with tarfile.open(fileobj=io.BytesIO(z.read(member))) as tar:
                files = [m for m in tar.getmembers() if m.isfile() and '..' not in Path(m.name).parts]
                for m in files:
                    target = game / 'baseset' / Path(m.name).name
                    target.write_bytes(tar.extractfile(m).read())
# The sprite font needs no FreeType fonts; declining the survey skips the modal
# question on the first start.
(game / 'openttd.cfg').write_text('[misc]\nprefer_sprite_font = true\n\n[network]\nparticipate_survey = no\n')
(game / 'openttd.args.txt').write_text(ARGUMENTS + '\n')

# The i386 DLLs openttd.exe loads at startup, built from the PE tree: its
# imports, theirs, and the DLLs that imported functions are forwarded to. The
# loader loads a forward's DLL while resolving the import; without it the
# function becomes a stub (advapi32's SystemFunction036, the CRT's and
# bcrypt's random numbers, forwards to cryptbase).
syswow64 = stage / 'drive_c/windows/syswow64'
queue, staged = [game / 'openttd.exe'], {}

def readobj(option, path):
    return subprocess.check_output([str(toolchain / 'llvm-readobj'), option, str(path)], text=True)

def imports_of(path):
    blocks = re.findall(r'^Import \{\n(.*?)^\}', readobj('--coff-imports', path), re.M | re.S)
    return [(re.search(r'Name: (.+)', block).group(1).lower(), set(re.findall(r'Symbol: (\S+) \(', block)))
            for block in blocks]

@functools.lru_cache(maxsize=None)
def forwards_of(path):
    return dict(re.findall(r'^  Name: (\S+)\n  ForwardedTo: ([^.\s]+)\.', readobj('--coff-exports', path), re.M))

def stage_dll(name):
    if name not in staged:
        module = name.removesuffix('.dll')
        assert re.fullmatch(r'[a-z0-9_-]+', module), name
        target = f'dlls/{module}/i386-windows/{name}'
        subprocess.run(['make', '-C', str(pe), '-j8', target], env=env, check=True)
        shutil.copy2(pe / target, syswow64 / name)
        staged[name] = pe / target
        queue.append(pe / target)
    return staged[name]

while queue:
    for name, symbols in imports_of(queue.pop()):
        forwards = forwards_of(stage_dll(name))
        for symbol in sorted(symbols & forwards.keys()):
            stage_dll(forwards[symbol].lower() + '.dll')

readme = (stage / 'README.txt').read_text()
(stage / 'README.txt').write_text(readme + '''
OpenTTD 15.3 (32-bit, with OpenGFX 8.0 and OpenSFX 1.0.3) is in C:\\openttd. Choose
C:\\openttd\\openttd.exe in the menu. openttd.args.txt next to it selects OpenGL video
without a drawing thread, sound effects through the win32 sound driver, no music, a 1280x720 window
and C:\\openttd\\openttd.cfg (sprite font). OpenGL runs on the Switch GPU through Mesa; putting
-v win32:no_threads in that file goes back to GDI drawing. Networking is unavailable: its DLLs load,
but report every call as unsupported.
Expected first milestone: the OpenTTD main menu with the title game running behind it.
''')

subprocess.run([sys.executable, str(probe / 'tools/verify-wow64-package.py'), str(stage)], check=True)
exe_info = subprocess.check_output([str(toolchain / 'llvm-readobj'), '--coff-imports', str(game / 'openttd.exe')], text=True)
assert 'Arch: i386\n' in exe_info
imports = {n.lower() for n in re.findall(r'^Import \{\n  Name: (.+)$', exe_info, re.M)}
staged = {p.name.lower() for p in syswow64.glob('*.dll')}
assert imports <= staged, f'OpenTTD imports not staged: {imports - staged}'
assert 'cryptbase.dll' in staged, 'advapi32 forwards SystemFunction036 to cryptbase'
assert list((game / 'baseset').rglob('opengfx.obg')), 'OpenGFX is missing'
assert (game / 'lang/english.lng').is_file()

# The full package stages every checkpoint over the one before it and has only one
# archive to give; asked for a stage alone, this leaves its own unwritten.
stage_only = os.environ.get('WINE_NX_STAGE_ONLY') == '1'
if stage_only:
    print(stage_root)
else:
    archive = build / 'wine-nx-openttd-dynarec-41.zip'
    with ZipFile(archive, 'w', ZIP_DEFLATED) as z:
        for f in sorted(stage.rglob('*')):
            if f.is_file() and f.name != '.DS_Store' and f.suffix != '.log':
                z.write(f, f.relative_to(stage_root))
    with ZipFile(archive) as z:
        assert z.testzip() is None
    print(archive)
