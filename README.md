# Wine-NX Switch

Wine-NX is a Nintendo Switch focused Wine bring-up. The goal is to run
AArch64 Windows PE programs inside a Switch NRO, using libnx/Horizon services
for process, memory, input, files, display, and eventually GPU presentation.

This repository is no longer just a primitive Horizon probe. It is the working
tree for the Switch Wine runtime, the in-process Horizon/Wine server work, the
win32u Switch display driver, and the SD-card package builder used for hardware
testing.

## Current Status

As of the latest runtime work, the project can build and stage a Switch NRO
runtime that launches Wine PE targets from:

```text
sdmc:/switch/wine
```

Notepad is the main real-GUI test target right now. It can launch far enough to
show the main window, frame, menu bar, menu popups, text rendering, and touch
driven menu interaction. It is still experimental and not production usable:
responsiveness is poor, presentation is software-heavy, and several real-app
subsystems remain incomplete.

## What Works So Far

### Switch Build And Packaging

- `wine-nx-probe/build-switch.sh` builds the Switch runtime with the
  `devkitpro/devkita64` Docker image.
- The script stages a runnable SD package under:

```text
wine-nx-probe/build-switch/sd-card/switch/wine
```

- Runtime NRO output:

```text
wine-nx-probe/build-switch/wine-nx-runtime.nro
wine-nx-probe/build-switch/sd-card/switch/wine/wine-nx-runtime.nro
```

- `WINE_NX_APP` selects the staged target:

```sh
WINE_NX_APP=gui ./wine-nx-probe/build-switch.sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

- The package now stages Wine NLS files and Wine fonts into the Switch package,
  including `C:\windows\fonts` and `share/wine/fonts` equivalents.

### Runtime Loader

- The runtime can bootstrap a Wine ARM64 PE target from the staged package.
- PE import resolution, loader handoff, Wine DLL staging, and runtime target
  selection are in place for the smoke apps and Notepad path.
- The runtime carries a build marker in the NRO so hardware logs can confirm
  which binary is actually running.
- Logging goes to:

```text
sdmc:/switch/wine/wine-nx-runtime.log
sdmc:/switch/wine/horizon-trace.log
```

### Horizon And Wine Server Substrate

- Early Horizon primitives were proven: TLS model, executable memory/JIT alias
  handling, thread setup, address-space reservation, and Switch-safe runtime
  logging.
- The in-process Horizon/Wine server path is active enough to service the
  current NTDLL and USER/GDI flows.
- Win32 syscall dispatch is working for the exercised `win32u` calls.
- USER server work has enough coverage for the GUI path:
  - atoms;
  - window station and desktop basics;
  - class creation/registration;
  - top-level and popup window objects;
  - shared-session style locators/data sufficient for current windows.

### GDI And USER Bring-Up

- GDI software rendering is working through Wine's DIB path.
- The GUI smoke app proved DC creation, compatible DCs, DIB sections, bitmaps,
  StretchDIBits-style raster work, object deletion, and cursor/menu drawing.
- `RegisterClassW` and `CreateWindow` moved past the original server-object
  blockers.
- Basic Notepad non-client rendering works:
  - main frame;
  - title/action buttons;
  - menu bar;
  - menu popup surfaces;
  - text through FreeType-backed Wine fonts.

### Switch Display Driver

- `dlls/win32u/winnx_drv.c` is the Switch-specific win32u display driver.
- It reports a single 1280x720 primary monitor to Wine.
- Wine window surfaces render into normal software DIB memory, then the driver
  blits dirty pixels to the libnx framebuffer.
- Popup/menu surface restore was added so old menu pixels can be covered again
  when popups hide or move.
- Dirty-present work avoids some full-surface flushes.
- Batched-present work keeps a pending framebuffer open and presents at
  higher-level boundaries instead of queueing every tiny dirty rectangle.

### Input

- Switch touchscreen input is sampled through libnx.
- Touch is translated into absolute Win32 mouse input.
- Non-client hit tests work well enough to interact with the Notepad frame and
  top menus.
- `get_window_children_from_point` support was added for correct child-window
  hit routing.

### Real GUI Milestone

The current real-app milestone is Wine Notepad:

- window appears on Switch;
- frame/control glyphs render correctly;
- menu fonts render;
- top menu interaction is partially working via touch;
- popups can be drawn and dismissed;
- the app is visibly alive, but slow and not yet a comfortable UI.

## Current Problems

### Presentation Is Still Too Slow

CPU headroom remains, which strongly suggests the bottleneck is presentation
and synchronization, not raw CPU rasterization.

The current libnx Framebuffer path uses `framebufferMakeLinear()`. That makes
the driver easy to write, but every `framebufferEnd()` converts the full
1280x720 shadow buffer into block-linear layout before queueing it to the
Switch compositor. Even if Wine only changes a small menu highlight, the
present path can still become expensive.

The current batching patch reduces how often this happens, but the real
performance step is a GPU presentation path. Since this tree already has a
Mesa/NVK Vulkan port available, the preferred direction is a Vulkan/NVK
compositor: upload Wine DIB/window surfaces into GPU textures, composite them,
and present once per frame.

### UI Completeness Is Still Early

Known rough areas:

- menus are functional but not fully Windows-perfect;
- popup/menu z-order and invalidation still need more compositor logic;
- touch input needs better capture, focus, drag, and double-click behavior;
- no controller/keyboard text-input path yet;
- no real GPU acceleration yet;
- Notepad works as a milestone, not as proof that arbitrary GUI apps are ready.

### Wine Subsystem Gaps

Real applications will need more work in:

- USER message queue and window state edge cases;
- common controls and dialogs;
- shell/comdlg/shell32 behavior;
- COM/OLE paths used by many apps;
- registry and prefix behavior;
- font discovery/fallback beyond the staged Wine font set;
- clipboard, IME, keyboard, and controller input;
- audio, networking, and multi-process behavior as later milestones.

## Build

### Host System Setup

The Switch NRO build runs inside Docker with the `devkitpro/devkita64` image.
The script installs missing Switch portlibs inside that container when needed,
including FreeType/Harfbuzz for font rendering.

The host-side Wine PE rebuild path uses LLVM-MinGW. The default expected path is
ignored by git:

```text
wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal
```

If `wine-nx-probe/toolchains/` is missing, restore it from the pinned
LLVM-MinGW release:

```sh
mkdir -p wine-nx-probe/toolchains
curl -L -o wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal.tar.xz \
  https://github.com/mstorsjo/llvm-mingw/releases/download/20260505/llvm-mingw-20260505-ucrt-macos-universal.tar.xz
tar -C wine-nx-probe/toolchains \
  -xf wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal.tar.xz
```

For another host/toolchain layout, point the build at an extracted LLVM-MinGW
directory:

```sh
LLVM_MINGW_DIR=/path/to/llvm-mingw WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

The ARM64 PE Wine build trees are also ignored by git:

```text
wine-nx-probe/build-wine-arm64-pe-clean
wine-nx-probe/build-wine-arm64-pe-local
```

Those directories are disposable Wine PE build trees used to rebuild Notepad
and Wine PE DLLs. `wine-nx-probe/build-wine-arm64-pe-clean` is the default PE
build directory. When `WINE_NX_APP=notepad` and
`wine-nx-probe/build-wine-arm64-pe-local` exists, `build-switch.sh` prefers the
local tree for the staged program. Override either path with:

```sh
WINE_NX_PROGRAM_BUILD_DIR=/path/to/wine-pe-build WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

To recreate a PE build tree from the repository root:

```sh
PE_BUILD_DIR=wine-nx-probe/build-wine-arm64-pe-clean
mkdir -p "$PE_BUILD_DIR"
(
  cd "$PE_BUILD_DIR"
  PATH="$PWD/../toolchains/llvm-mingw-20260505-ucrt-macos-universal/bin:$PATH" \
    ../../configure \
      --enable-archs=aarch64 \
      --disable-tests \
      --without-x \
      --without-freetype \
      --without-fontconfig \
      --without-gettext \
      --without-gnutls \
      --without-opengl \
      --without-vulkan \
      --without-sdl \
      --without-cups \
      --without-coreaudio \
      --without-alsa \
      --without-pulse \
      --without-gstreamer \
      --without-ffmpeg \
      --without-dbus \
      --without-gphoto \
      --without-gssapi \
      --without-krb5 \
      --without-netapi \
      --without-opencl \
      --without-pcap \
      --without-pcsclite \
      --without-sane \
      --without-usb \
      --without-v4l2 \
      --without-wayland \
      --without-unwind \
      --with-mingw=llvm-mingw
)
```

After configure, the normal Switch package build will compile the missing
Notepad executable and DLLs from that tree.

From the repository root:

```sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

Other useful targets:

```sh
WINE_NX_APP=gui ./wine-nx-probe/build-switch.sh
WINE_NX_APP=curl ./wine-nx-probe/build-switch.sh
```

The staged SD package is written to:

```text
wine-nx-probe/build-switch/sd-card/switch/wine
```

Copy or sync that package to:

```text
sdmc:/switch/wine
```

The package helper prints:

```text
Sync mounted SD with: wine-nx-probe/tools/sync-switch-wine-package.sh
```

## Hardware Test Checklist

1. Build the package with the target you want, usually:

```sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

2. Deploy `wine-nx-runtime.nro` and the staged `switch/wine` package.

3. Run on Switch.

4. Confirm the runtime log starts with the expected marker, for example:

```text
[BUILD] nx-batched-present-1
```

5. Check:

```text
sdmc:/switch/wine/wine-nx-runtime.log
sdmc:/switch/wine/horizon-trace.log
```

The marker matters. Several performance/debug loops looked confusing until the
logs showed an older NRO was still being run.

## Important Files

Runtime and packaging:

```text
wine-nx-probe/build-switch.sh
wine-nx-probe/source/runtime.c
wine-nx-probe/source/runtime_platform.c
wine-nx-probe/CMakeLists.txt
```

Switch display and USER/GDI integration:

```text
dlls/win32u/winnx_drv.c
dlls/win32u/window.c
dlls/win32u/dce.c
dlls/win32u/sysparams.c
dlls/win32u/message.c
dlls/win32u/input.c
```

Horizon/NTDLL substrate:

```text
dlls/ntdll/unix/horizon.c
dlls/ntdll/unix/file.c
dlls/ntdll/unix/process.c
dlls/ntdll/unix/thread.c
dlls/ntdll/unix/signal_arm64.c
```

Smoke targets:

```text
wine-nx-probe/samples/gui-smoke
wine-nx-probe/samples/curl-arm64
```

## Next Milestones

### Experimental x86 Execution

The native ARM64 Wine / Box64 WoW64 integration is tracked in
[the CPU interface document](documentation/wow64-box64-interface.md).
The pinned interpreter now runs an i386 sequence through the native transition
bridge in an ARM64 Linux test, and the same smoke test builds as a Switch NRO:

```sh
sh wine-nx-probe/check-box64-execution.sh
```

This is an execution-core milestone. PE32 loading, a selectable CPU DLL and
game compatibility are not enabled yet; the ARM64 Wine runtime remains the
default. The document lists the tested behavior and remaining state/fault work.

1. Presentation performance

Replace or bypass the expensive linear framebuffer path. Preferred direction:

- Vulkan/NVK compositor for Wine software window surfaces;
- DIB/window surface upload into GPU textures;
- popup/window composition on GPU;
- one present per frame;
- DXVK/vkd3d later, after Wine's Vulkan path is clean enough for real
  D3D-to-Vulkan acceleration.

Fallback or lower-level options:

- deko3d compositor;
- direct block-linear dirty conversion;
- persistent software backing store plus one present per frame.

2. Input polish

- keyboard/text input;
- controller mouse mode;
- better touch capture and focus;
- double-click/drag behavior;
- native `WM_TOUCH` later.

3. USER/window manager behavior

- popup/menu stacking;
- owner/activation edge cases;
- clipping and invalidation;
- modal dialogs;
- child-window ordering.

4. More real apps

After Notepad is smoother, the next useful test targets should be small
non-network GUI apps that exercise dialogs, common controls, edit controls, and
file browsing without requiring a browser engine or GPU API first.

The experimental ARM64 CPU DLL is now in `dlls/winebox64`. With an ARM64/i386
Wine PE build configured as described in
[the interface notes](documentation/wow64-box64-interface.md), run
`sh wine-nx-probe/build-wow64-components.sh` to build and stage its components.
The original interpreter gate test passed on Switch hardware. The new package
autoruns a minimal PE32 through Wine’s WoW64 startup; that full path is still
unverified on hardware. Success is a logged exit code of `0x0000002a`.

The minimal PE32 test subsequently passed on Switch hardware with exit `0x2a`,
followed by the functional test (time, file I/O, heaps, dynamic TLS) and the
two-worker thread test (`nx-wow64-threads-2`).

The current package runs a real x86 console application, 7-Zip's `7zr.exe`,
testing a known archive. On Switch hardware (`nx-wow64-console-2`) it ran to
completion with exit code 0, 7-Zip's no-error result, at about 4.2 million x86
instructions per second. That build's log did not capture the program's text.
Build `nx-wow64-console-3` copies every write to stdout/stderr into the log as
`[STDOUT]`/`[STDERR]` lines, and `[BOX64]` lines report interpreter speed. On
hardware it showed 7zr's full output, including `Everything is Ok`, but reported
the archive as 0 bytes: directory listings carried names only. Build
`nx-wow64-console-4` filled in sizes, times and attributes and ran `7zr a` on a
staged folder tree; 7zr created its archive but could not open the folder itself.
Build `nx-wow64-console-5` opens directories the way NT does and stops reporting
every directory as a mount point; on hardware `7zr a` then archived the whole tree
(`3 folders, 3 files, 325691 bytes`, `Everything is Ok`, exit 0). Build
`nx-wow64-console-6` added directory creation, file times and attributes for
`7zr x`. Its hardware run hit 7-Zip's error path instead and showed that x86
exception dispatch could not resume in the interpreter. Build
`nx-wow64-console-7` fixed that (16-bit selectors, Eax after `NtContinue`); on
hardware 7-Zip's error path then threw and caught its C++ exception and exited
with code 2, exactly as on Windows. Build `nx-wow64-console-8` added
`SetEndOfFile` to the Horizon server; on hardware `7zr x` then extracted a
staged archive (3 folders, 3 files, every CRC matching). Build
`nx-wow64-console-9` added rename and delete dispositions to the Horizon server;
on hardware `7zr rn` then rewrote an archive in place. The staged package now
runs the two-thread 7-Zip benchmark (`7zr b 1 -mmt2 -md18`). Its first hardware run
showed x86 programs being told the CPU was ARM with zero processors; build
`nx-wow64-console-10` reports the emulated x86 processor and the real core count. The thread-lifecycle test below passed first and stays
staged as a regression target.

The lifecycle test is `pe32-lifecycle.exe` (build `nx-wow64-lifecycle-2`).
It covers the complete x86 thread lifecycle: workers that return or call
`ExitThread`, joins that wait for the real end, exit codes, CREATE_SUSPENDED,
static and dynamic TLS, and 48 rounds of synchronized workers. Success is
`[PE32 TEST] PASS ALL`, `[LIFECYCLE] verdict=PASS` (thread stacks, TEBs, server
objects, pipes and interpreter engines back at baseline) and exit `0x2a`. It
passed on Switch hardware: 202 x86 threads created, joined and reclaimed; see the
[interface notes](documentation/wow64-box64-interface.md) for details.
