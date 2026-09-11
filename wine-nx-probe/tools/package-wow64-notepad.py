#!/usr/bin/env python3
"""Build i386 Wine Notepad and stage its import closure with the dynarec NRO."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
from zipfile import ZipFile, ZIP_DEFLATED
probe = Path(__file__).resolve().parents[1]
root = probe.parent
pe = probe / 'build-wine-wow64-pe'
build = probe / 'build-switch-wow64-dynarec'
baseline = probe / 'build-switch-wow64/sd-card/switch/wine'
stage = build / 'notepad-sd-card/switch/wine'
toolchain = probe / 'toolchains/llvm-mingw-20260505-ucrt-macos-universal/bin'
env = dict(os.environ, PATH=f'{toolchain}:/opt/homebrew/opt/bison/bin:' + os.environ['PATH'])
verify = probe / 'tools/verify-wow64-package.py'
subprocess.run([sys.executable, str(verify), str(baseline)], check=True)
subprocess.run(['make', '-C', str(pe), '-j8', 'programs/notepad/i386-windows/notepad.exe'], env=env, check=True)
shutil.copytree(baseline, stage, dirs_exist_ok=True, ignore=shutil.ignore_patterns('*.log', '.DS_Store'))
shutil.copy2(build / 'wine-nx-runtime.nro', stage / 'wine-nx-runtime.nro')
for module in ('ntdll', 'wow64', 'wow64win', 'winebox64', 'win32u'):
    shutil.copy2(pe / f'dlls/{module}/aarch64-windows/{module}.dll',
                 stage / 'drive_c/windows/system32' / f'{module}.dll')
exe = pe / 'programs/notepad/i386-windows/notepad.exe'
shutil.copy2(exe, stage / 'drive_c/notepad.exe')
queue = [exe]
seen = set()
# Explicit process-attach dependencies used by the existing GUI path.
extra = ['imm32.dll']
while queue or extra:
    names = extra
    extra = []
    if queue:
        info = subprocess.check_output([str(toolchain / 'llvm-readobj'), '--coff-imports', str(queue.pop())], text=True)
        names += re.findall(r'^Import \{\n  Name: (.+)$', info, re.M)
    for name in names:
        name = name.lower()
        if name in seen:
            continue
        seen.add(name)
        module = name.removesuffix('.dll')
        assert re.fullmatch(r'[a-z0-9_-]+', module), name
        target = f'dlls/{module}/i386-windows/{name}'
        subprocess.run(['make', '-C', str(pe), '-j8', target], env=env, check=True)
        dll = pe / target
        shutil.copy2(dll, stage / 'drive_c/windows/syswow64' / name)
        queue.append(dll)
fonts = list((root / 'fonts').glob('*.ttf'))
assert fonts, 'The existing GUI path needs Wine fonts'
for folder in ['drive_c/windows/fonts', 'share/wine/fonts']:
    (stage / folder).mkdir(parents=True, exist_ok=True)
    for font in fonts:
        shutil.copy2(font, stage / folder / font.name)
(stage / 'target.txt').write_text('sdmc:/switch/wine/drive_c/notepad.exe\n')
(stage / 'args.txt').write_text('C:\\notepad.exe C:\\notepad-test.txt\n')
(stage / 'drive_c/notepad-test.txt').write_bytes(b'Wine-NX x86 Notepad through the ARM64 dynarec.\r\n\r\nMove the cursor with the right stick. A is the left mouse button, B the right.\r\nOpen a menu, select text by holding A, right-click with B, and try Save As.\r\n')
(stage / 'README.txt').write_text('''Wine-NX x86 programs on the nx-wow64-dynarec-17 runtime.

Starting Wine-NX shows a menu of the Windows programs (.exe) in
sdmc:/switch/wine/drive_c and its folders, marked x86 or ARM64:
  Up/Down (D-pad or left stick) choose, L/R page, A start, + quit,
  Y turns verbose logs on or off (for bug reports).
The menu opens on the last program started. A program's own arguments go in a file
next to it (openttd.exe reads openttd.args.txt). Otherwise args.txt is used when its
first word names the chosen program: it holds C:\\notepad.exe C:\\notepad-test.txt,
so Notepad opens its test document and other programs start without arguments.
To pick another program, close Wine-NX from HOME and start it again.

Controls in programs: the right analog stick moves the mouse cursor, A is the left
button and B the right; hold A while moving to drag or select. Touching the screen
clicks and moves the cursor there. Menus from the menu bar take the left button (A).

Try in Notepad: the blinking caret, Edit > Cut/Copy/Paste, the right-click menu (B),
Format > Font..., Format > Word Wrap and Search > Find.
Tests in the menu (each ends with [PE32 TEST] PASS ALL and exit_code=0x0000002a in
wine-nx-runtime.log): pe32-messages.exe (messages between threads, message waits,
clipboard), pe32-timers.exe (window timers), pe32-lifecycle.exe (threads).

Verified on hardware: cursor and buttons, menus, Word Wrap, the Format > Font dialog
(build 11), and in pe32-messages.exe on build 15 messages between threads,
ReplyMessage, SendMessageCallback, GetQueueStatus, PostQuitMessage and the clipboard.
Build 16 fixes MsgWaitForMultipleObjects returning at once after another thread's
message had been processed. Timers and the caret await hardware confirmation.

Logs: sdmc:/switch/wine/wine-nx-runtime.log, and horizon-trace.log with verbose logs.
Install by merging switch/ into the SD root.
''')
subprocess.run([sys.executable, str(verify), str(stage)], check=True)
archive = build / 'wine-nx-notepad-dynarec-17.zip'
with ZipFile(archive, 'w', ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file() and f.name != '.DS_Store' and f.suffix != '.log':
            z.write(f, f.relative_to(build / 'notepad-sd-card'))
with ZipFile(archive) as z:
    assert z.testzip() is None
print(archive)
