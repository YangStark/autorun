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
(stage / 'README.txt').write_text('''x86 Wine Notepad on the nx-wow64-dynarec-11 runtime.
Build 11 turns verbose traces off by default: system calls, Horizon server
requests (horizon-trace.log), fonts and window painting are no longer written to
the SD card as they happen, which made everything slower. To turn them back on
for a bug report, create sdmc:/switch/wine/verbose.txt containing 1.
wine-nx-runtime.log shows "[INIT] verbose traces off" or "on".
Mouse cursor: the right analog stick moves an arrow drawn over the screen,
A is the left mouse button and B the right. Hold A while moving to drag or select.
Small tilts move slowly for precise placement; a full tilt crosses the screen in
about a second. Touching the screen still clicks and moves the cursor there.
Build 10 adds window lists to the Horizon server. GetDlgItem lists a dialog's
children through them; the server rejected the request, so every GetDlgItem
returned NULL. The Font dialog then filled and hid nothing: empty Font, Style,
Size and Script boxes, and a visible Color box that should be hidden.
Sibling order no longer breaks at a combobox's own child windows.
Build 9 (posted messages: Word Wrap works on hardware) and build 8 are kept.
Expected: Format > Font... lists the installed fonts, with styles, sizes and scripts
for the selected font, and no Color box; picking a font and OK changes the text.
With verbose.txt, horizon-trace.log shows [HZUSER] get_window_list lines while the dialog opens.
Build 6 fixes are kept: menu switching restores the owner and hidden popups stay hidden.
Uses the existing Switch software display, touch input and FreeType fonts.
The previous Notepad package was ARM64; this executable and its GUI DLLs are i386.
Expected first milestone: Notepad frame, menus and this test document visible.
Then exercise touch/menu input and Save As. These paths need hardware confirmation.
Native-entry telemetry in wine-nx-runtime.log confirms dynarec execution.
The console yields the screen to the framebuffer; logs continue on SD.
Install by merging switch/ into the SD root. This selects Notepad in /switch/wine.
Reinstall the 7zr or interpreter package to restore that launch configuration.
''')
subprocess.run([sys.executable, str(verify), str(stage)], check=True)
archive = build / 'wine-nx-notepad-dynarec-11.zip'
with ZipFile(archive, 'w', ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file() and f.name != '.DS_Store' and f.suffix != '.log':
            z.write(f, f.relative_to(build / 'notepad-sd-card'))
with ZipFile(archive) as z:
    assert z.testzip() is None
print(archive)
