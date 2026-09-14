# Wine-NX

Wine for the Nintendo Switch. A homebrew NRO runs Windows programs on Horizon
through libnx: 32-bit x86 programs through Wine's WoW64 with Box64 as the CPU
backend, and ARM64 programs natively. The Wine server, memory management,
exceptions and the display and input drivers are reimplemented for Horizon and
run inside the same process.

It is experimental. Programs that work are listed below; anything else is
likely to stop on a missing piece, and the runtime log says which.

## Status

Verified on hardware:

| Program | State |
|---|---|
| 7-Zip `7zr.exe` | Test, add, extract, rename and the benchmark all complete. |
| Wine Notepad (x86) | Menus, dialogs, fonts and the controller cursor work. |
| OpenTTD 15.3 (x86) | Runs at up to 60 fps with OpenGL, with sound effects. |
| Direct3D 9 test | Creates a device and draws, about 55 fps. |
| Need for Speed Underground 2 | In game with sound and the controller (NFSU-XtendedInput). Races run at roughly 20–45 fps at default detail with the CPU overclocked. |

In progress: WarCraft III loads its game DLL and creates its Direct3D 9 device,
then needs DirectShow for its intro movies (see
[Limits](#limits)).

## What is in place

- **x86 execution.** `dlls/winebox64` is the WoW64 CPU DLL. It runs Box64's
  ARM64 dynarec with separate writable and executable code mappings, as Horizon
  requires, including Box64's call/return optimization. System calls and unix
  calls from x86 code are dispatched without leaving the emulator loop.
- **Wine on Horizon.** An in-process Wine server
  (`dlls/ntdll/unix/horizon*.c|h`) covers files, directories, sync objects,
  threads, the registry (`system.reg`/`user.reg` on the card), message queues,
  timers, the clipboard, raw input and object directories.
- **Graphics.** GDI draws to the framebuffer. OpenGL goes through Mesa 20.1's
  nouveau driver, patched for pinned 32-bit buffers and buffer reuse
  (`wine-nx-probe/mesa`). Direct3D 9 runs on that OpenGL through wined3d.
- **Audio.** `winenxaudio.drv` plays through audout, for mmdevapi and DirectSound.
- **Input.** The touchscreen, the controller as a mouse (right stick, A and B)
  or as a keyboard, and XInput, which sees player 1 as an Xbox 360 controller.
- **Memory.** Fixed-base games such as NFSU2 need a 32-bit address space:
  launch the NRO through a forwarder set to "32-bit, no alias", which also
  raises the memory limit to 2 GiB. Wine's allocations stay clear of the memory
  Horizon hands to libnx.
- **Diagnostics.** A thread and core report, a sampling profiler, and fatal
  fault reports that name the x86 instruction behind translated code.

## Using it

Everything lives in `sdmc:/switch/wine`: the runtime `wine-nx-runtime.nro`,
Wine's files, and `drive_c` with the programs.

Starting the NRO opens a launcher that lists the `.exe` files in `drive_c` and
two folder levels below it:

| Button | Action |
|---|---|
| A | Start the selected program |
| Up/Down, L/R | Choose, page |
| Y | Verbose logs on or off (`verbose.txt`) |
| X | Profiler on or off (`profile.txt`) |
| + | Quit |

A program can have its own files next to its executable, named after it:

| File | Content |
|---|---|
| `NAME.args.txt` | Its command-line arguments |
| `NAME.keys.txt` | Controller-to-key mapping, over `keys.txt` |
| `NAME.box64.txt` | Box64 code generation options, one `BOX64_DYNAREC_*=value` per line |

Each run writes `wine-nx-runtime.log`. Its first lines include `[BUILD]`, the
runtime version, worth checking before reading anything else. `[PROGRESS]`
lines report every 10 seconds: frames, OpenGL and system call rates, memory and
translation counters. With the profiler on, `[THREADS]`, `[SERVER]` and `[PROF]`
show where each busy thread spends its time. A few more files in the same
folder change behavior, for testing: `no-balance.txt` (core balancing),
`gl-uncached.txt`, `gl-noclean.txt` and `no-display-devices.txt`.

## Building

Requirements:

- Docker with the `devkitpro/devkita64` image, for the Switch build and the Box64
  tests.
- LLVM-MinGW 20260505 in `wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal`,
  and bison (Homebrew's), for Wine's PE modules.
- A Wine PE build tree, configured once:

```sh
mkdir -p wine-nx-probe/build-wine-wow64-pe && cd wine-nx-probe/build-wine-wow64-pe
PATH="$PWD/../toolchains/llvm-mingw-20260505-ucrt-macos-universal/bin:/opt/homebrew/opt/bison/bin:$PATH" \
    ../../configure --enable-archs=aarch64,i386 --enable-winebox64=aarch64
```

- Optionally, the patched Mesa: `sh wine-nx-probe/build-switch-mesa.sh`. The
  runtime links it whenever its install folder exists, and devkitPro's Mesa
  otherwise.

Then, from the repository root:

```sh
sh wine-nx-probe/build-wow64-dynarec.sh           # Wine modules, the runtime NRO and its package
python3 wine-nx-probe/tools/package-wow64-full.py # the whole SD-card payload as one zip
```

The runtime's version is `WINE_NX_RUNTIME_BUILD` in
`wine-nx-probe/source/runtime.c`, and archives are written to
`wine-nx-probe/build-switch-wow64-dynarec`. Other packagers in
`wine-nx-probe/tools` stage single programs over the full payload (OpenTTD,
Quake III's engine, the Direct3D 9, OpenGL and audio tests).
`package-wow64-dll-overlay.py --log wine-nx-runtime.log` builds the i386 DLLs a
run reported missing, with everything they import, as an overlay zip.

## Tests

```sh
sh wine-nx-probe/check-runtime-console.sh  # host unit tests of runtime and server pieces, under ASan and UBSan
sh wine-nx-probe/check-box64-execution.sh  # Box64 interpreter and dynarec in an ARM64 container, plus their Switch build
sh wine-nx-probe/check-audio.sh            # the audio driver
```

Box64 is pinned in `wine-nx-probe/vendor/box64` (fetched by
`tools/bootstrap-box64-core.sh`) and never edited:
`wine-nx-probe/cmake/Box64Core.cmake` builds patched copies of the files it
changes, and fails if the pinned text moves.

## Layout

| Path | Content |
|---|---|
| `dlls/ntdll/unix/horizon*` | Horizon server, memory, exception handling |
| `dlls/win32u/winnx_drv.c` | Display and input driver |
| `dlls/winebox64` | WoW64 CPU DLL |
| `wine-nx-probe/source` | Runtime: startup, launcher, Box64 engine, profiler, audio and XInput backends |
| `wine-nx-probe/tests` | Host and PE32 tests |
| `wine-nx-probe/tools` | Packagers |

## Limits

- Speed. Heavy Direct3D games are limited by translated x86 code on the game's
  main thread and by Wine's Direct3D layer, rather than by the GPU.
- Wine's first-run setup (wineboot) does not run, so COM components are not
  registered. DirectShow, which WarCraft III uses, has to be registered once by
  running `regsvr32.exe /s quartz.dll devenum.dll quartz.dll` from the launcher
  (quartz needs devenum registered to finish, hence twice).
- One program at a time; no Vulkan; one controller.
- Missing DLLs: the card only holds what earlier programs needed. The log names
  what is missing, and the overlay packager builds it.

## More

- [WoW64 CPU interface](documentation/wow64-box64-interface.md)
- [x86 memory layout on Horizon](documentation/horizon-x86-memory.md)
- [Build-by-build notes](wine-nx-probe/README.md)
