#!/usr/bin/env python3
"""Stage everything an SD card needs for this build in one archive: the Wine
payload with its test programs, OpenTTD, the audio checkpoint and the OpenGL
checkpoint, with OpenTTD preselected in the launcher.

Each checkpoint packager runs over the previous one's stage, so the result holds
every program they stage; the build number comes from the runtime's marker."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import os
import re
import shutil
import subprocess
import sys

probe = Path(__file__).resolve().parents[1]
build = probe / 'build-switch-wow64-dynarec'
tools = probe / 'tools'
marker = re.search(r'nx-wow64-dynarec-(\d+)', (probe / 'source/runtime.c').read_text()).group(1)
stage_root = build / 'full-sd-card'
stage = stage_root / 'switch/wine'

def package(script, **base):
    subprocess.run([sys.executable, str(tools / script)], check=True, env=dict(os.environ, **base))

package('package-wow64-notepad.py')
package('package-wow64-openttd.py')
shutil.rmtree(build / 'audio-sd-card', ignore_errors=True)
package('package-wow64-audio.py', WINE_NX_AUDIO_BASE=str(build / 'openttd-sd-card/switch/wine'))
package('package-wow64-opengl.py', WINE_NX_OPENGL_BASE=str(build / 'audio-sd-card/switch/wine'))
package('package-wow64-d3d9.py', WINE_NX_D3D9_BASE=str(build / 'opengl-sd-card/switch/wine'))

# The checkpoint READMEs describe one checkpoint each; this package has its own.
shutil.rmtree(stage_root, ignore_errors=True)
shutil.copytree(build / 'd3d9-sd-card/switch/wine', stage,
                ignore=shutil.ignore_patterns('*.log', '.DS_Store', '*-README.txt'))
# The launcher lists every program in drive_c; target.txt only preselects one.
(stage / 'target.txt').write_text('sdmc:/switch/wine/drive_c/openttd/openttd.exe\n')
(stage / 'run-entry.txt').write_text('1\n')
# The controller stands in for a keyboard; this lists what each control sends
# and how to change it, with every line commented out so the defaults hold.
(stage / 'keys.txt').write_text('''# Keys the controller sends, one NAME=code line each, where code is a Windows
# virtual-key code in decimal or 0x form. Remove the # to change one. A and B
# are not here: they stay the left and right mouse buttons.
#
# UP=0x26      d-pad up, or the left stick pushed up
# DOWN=0x28
# LEFT=0x25
# RIGHT=0x27
# X=0x20       space
# Y=0x46       f
# L=0x09       tab
# R=0x10       shift
# ZL=0x28      down arrow, a brake in a racing game
# ZR=0x26      up arrow, the accelerator
# PLUS=0x1B    escape
# MINUS=0x09   tab
# STICKL=0x11  control
# STICKR=0x12  alt
''')
(stage / f'BUILD-{marker}-README.txt').write_text(f'''Wine-NX build {marker}: the whole SD-card payload.
Copy the switch folder to the SD card, merging folders; it replaces the runtime
NRO and the Wine payload of any earlier build.

The launcher lists the programs in drive_c. OpenTTD is preselected:
C:\\\\openttd\\\\openttd.exe draws with OpenGL on the Switch GPU (Mesa), plays sound
effects through the win32 driver, and reads openttd.args.txt next to it; putting
-v win32:no_threads there goes back to GDI drawing.
Also staged: C:\\\\pe32-opengl.exe (red, green and blue frames, then PASS and
exit_code=0x0000002a), C:\\\\pe32-audio.exe (audout playback), C:\\\\notepad.exe and
the 7zr benchmark.

The screen: windows are now shown through OpenGL on the GPU, each in its own
layer drawn in stacking order, instead of copying their pixels straight to the
framebuffer. A program's own OpenGL (OpenTTD, Direct3D games) still takes the
whole screen while it draws; the windows come back when it stops. The log shows
"[INIT] windows shown by the OpenGL compositor" and "[NXCOMP]" lines. If windows
do not show or look wrong, put a file switch/wine/framebuffer.txt containing 1
on the SD card to go back to the framebuffer.

wine-nx-runtime.log holds the run. Its [PROGRESS] lines report OpenGL frames,
the time in eglSwapBuffers and in opengl32 calls, the megabytes Wine copies for
32-bit buffer mappings (copy_mb), whether the GPU maps the program's own pages
(pinned=1, or -1 with pin_rc when nvservices refused them), and the slowest
opengl32 calls of the last ten seconds.
''')
subprocess.run([sys.executable, str(tools / 'verify-wow64-package.py'), str(stage)], check=True)

archive = build / f'wine-nx-full-dynarec-{marker}.zip'
with ZipFile(archive, 'w', ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file() and f.name != '.DS_Store' and f.suffix != '.log':
            z.write(f, f.relative_to(stage_root))
with ZipFile(archive) as z:
    assert z.testzip() is None
print(archive)
