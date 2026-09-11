# Wine-NX Probe And Runtime

`nx-wow64-dynarec-11` makes per-operation traces opt-in. A build-9 Notepad
session wrote 72,065 runtime log lines: 64,322 `[SYSCALL]` lines (two per
system call), 5,954 `[NXFONT]`, and hundreds of window-painting traces. Each
was formatted and buffered for the SD card. `horizon_trace` opened, appended to
and closed `horizon-trace.log` for every line, for most server requests. These
sites now check `wine_nx_runtime_verbose` before formatting: system calls,
Horizon server traces, `[NTOPEN]`, `[IOCTL]`, `[AFD]`, `[NXFONT]`, freetype,
`[NXWIN]`, `[NXDCE]`, `[NXSURF]`, `[NXRESIZE]`, `[NXWINPROC]`, `[NXMOUSE]` and
the display driver. The runtime reads `sdmc:/switch/wine/verbose.txt` (`1`
enables them) and logs `[INIT] verbose traces on|off`. Build, loader, exception,
exit, lifecycle, thread-creation and periodic `[DYNAREC]` lines still appear.
Verified on hardware: Notepad, menus and the cursor became much faster. The
build-10 Font dialog fix, also in this build, now lists fonts.

`nx-wow64-dynarec-10` adds window lists to the Horizon server. On build-9
hardware, Word Wrap worked, which confirmed posted messages. The Font dialog
opened with empty Font, Style, Size and Script boxes and a visible Color box.
The log showed font enumeration succeeding (`NtGdiEnumFonts` returned TRUE on
its second call), followed by messages to the combobox that returned 0 without
reaching its window procedure. `NtUserBuildHwndList` had failed 110 times with
`STATUS_NOT_IMPLEMENTED`, and user32's `GetDlgItem` depends on it: after
`GW_CHILD` it asks the server's `get_window_list` for the siblings. Every
`GetDlgItem` therefore returned NULL. Fonts were sent to no window, and
`ShowWindow` hid nothing because Notepad does not request `CF_EFFECTS`.
`get_window_list` now follows `server/window.c`: top-level windows,
recursive children, siblings from a window, and the desktop window, with
optional thread filters and a full count for short buffers. `get_window_tree`
had taken next/previous siblings from neighbors in the global window list.
Comboboxes create child windows immediately, so a sibling chain broke at the
first combobox. Both now use the parent-filtered list order.
`tests/check_window_list.py` builds a Font-dialog window list and runs the
production helpers. Verified on hardware with build 11: the Font dialog lists
fonts.

`nx-wow64-dynarec-9` adds posted messages to the Horizon server. On build-8
hardware, Format > Font... and Format > Word Wrap chosen with A only closed
the menu. `track_menu` posts the chosen command as `WM_COMMAND`, and even a
thread posting to itself goes through the server's `send_message` request. The
Horizon server had no handler, so every post returned `STATUS_NOT_IMPLEMENTED`
and the log shows no command reaching Notepad. EndDialog's wake-up post and
PostQuitMessage had the same gap. `horizon_message_queue.h` now keeps posted
messages and matches them like `server/queue.c`: per thread, oldest first, by
message range and by window, including its children, or thread-only with
HWND -1. `get_message` returns posted messages before WM_QUIT, hardware input
and paints. Destroying a window or ending a thread drops its messages.
Messages sent to another thread still need replies and remain unimplemented.
`wine-nx-probe/tests/horizon_message_queue.c` runs under ASan/UBSan from
`check-runtime-console.sh`. Verified on hardware: Word Wrap toggles, and
Font... opens its dialog.

`nx-wow64-dynarec-8` fixes an input freeze found on hardware with build 7.
With Notepad's Format menu open, quick B presses on Font... were processed as
follows. Win32u peeked the third press, which was within the double-click
interval, as `WM_RBUTTONDBLCLK`. Menu tracking then removed it with a filter
containing only that message. The Horizon server matched queued mouse messages
against their plain and non-client forms only, never their double-click forms.
It therefore never returned the press for removal, and the menu loop kept
peeking it. The log shows about 34,000 identical `[NXMOUSE] hit ... in=204`
lines, and every later click was queued behind it. The server now matches the
same forms as Wine's `check_hw_message_filter`. `tests/check_mouse_filter.py`
fails on the build-7 filter and passes now. Fast left double-clicks inside a
menu had the same exposure. No Font dialog was requested in that session:
menu-bar menus track with `TPM_LEFTBUTTON`, so B on a menu item is ignored.
Font... has to be chosen with A. Verified on hardware: fast B presses in an
open menu no longer freeze input.

`nx-wow64-dynarec-7` adds a mouse cursor for controllers. The right analog
stick moves it, A is the left button and B the right. Touch input still works.
The runtime reads the stick and buttons through libnx's pad API. Motion is a
velocity past a 12% radial dead zone and grows with the square of the tilt,
reaching 1000 px/s, scaled by the time between polls. Horizon has no hardware
cursor, so the arrow is painted into the linear back buffer only for
`framebufferEnd` and the covered pixels are restored right after. Window blits
never capture it, and cursor-only frames are capped at 60 Hz. The Horizon
server now queues `WM_RBUTTONDOWN`/`WM_RBUTTONUP`, and `SetCursorPos` moves the
drawn cursor. `check-runtime-console.sh` tests the motion and sprite restore.
`tests/check_pointer_events.py` runs the driver's event translation for moves,
clicks, drags and touch. On hardware the cursor, A clicks and menu opening
worked; B inside an open menu exposed the freeze fixed in build 8.

`nx-wow64-dynarec-6` preserves zero PID/TID in the shared user-handle allocator.
Build 5 passed zero for synthetic desktop ownership, but the allocator silently
changed those IDs to one. Consequently the build-5 hardware log still has no
popup-owner restoration entries, and switching menus leaves old pixels behind.
Clicking the document repaints it through a separate path. The earlier ancestry
fixture mocked allocation and missed this conversion; it now includes the
production allocator and reproduces build 5's failure before the correction.
`check_desktop_ancestry.py --allocator-baseline` keeps the current desktop
handler but uses the old allocator to reproduce that specific failure.
Normal client PID/TID and handle metadata are also checked. The corrected
ancestry and popup framebuffer fixtures pass under ASan/UBSan. Hardware menu
switching remains to be verified with
`build-switch-wow64-dynarec/wine-nx-notepad-dynarec-6.zip`.

`nx-wow64-dynarec-5` attempted to fix popup restoration. The build-4 hardware logs show
16 popup hide callbacks but no owner restoration; the screenshot retains
Edit/Format/View pixels. Server-created desktop/message handles were labeled
with the requesting process ID despite having no local client WND. As a
result, get_win_ptr(desktop) returned NULL and GA_ROOT failed for Notepad,
discarding the popup's restoration owner. Synthetic desktop/message windows
were requested with server ownership (PID/TID zero), but the shared allocator
overrode that until build 6. The display driver presents after
hiding a popup and ignores late flushes from cached hidden surfaces.

`python3 wine-nx-probe/tests/check_desktop_ancestry.py` exercises the production
desktop handler, shared allocator and ancestor lookup; `--baseline` reproduces the zero root.
`python3 wine-nx-probe/tests/check_popup_restore.py` exercises the production
driver against a host framebuffer: restore covered pixels, present on hide,
suppress hidden flushes, switch menus and reopen a cached surface. Both use
ASan/UBSan. The build-5 hardware result and follow-up correction are recorded above.

`nx-wow64-dynarec-4` fixes ownership in the native Horizon callback bridge.
Callback arguments are copied before WoW64's in-place conversion. Return data
is copied before NtCallbackReturn unwinds, and remains owned by a per-thread,
per-nesting-depth slot until that depth is reused. Thread-key destructors free
all retained buffers. This prevents call_window_proc from freeing a packed
message that is also the returned geometry buffer, and prevents callbacks
from returning dangling native-stack data.

`python3 wine-nx-probe/tests/check_callback_lifetime.py --baseline` reproduces
the old bridge's aliasing. Without --baseline, the same fixture exercises the
fixed production functions under ASan/UBSan: caller-buffer free, input
isolation, stack return data, nested callbacks, repeated calls and two threads.
The build-4 hardware screenshot confirms correct full-screen edit sizing,
document text and status bar. Closed menu pixels still remain; build 5
addresses that separate restoration path.

Before build 4, Notepad reached a visible x86 GUI on hardware. That screenshot
shows its edit control retaining the initial 676x467 size inside the 1280x720
frame, and menu interaction is incorrect. `nx-wow64-dynarec-3` adds the missing
WoW64 GetPresentRect pointer conversion plus NXRESIZE/HZGEOM traces. The logs
also show malformed child client coordinates and a low-screen popup; the
relationship between these symptoms and the conversion gap is unconfirmed.
This is a diagnostic GUI package, not a hardware-confirmed layout/menu fix.

Notepad dynarec checkpoint: the first x86 GUI run loaded fonts and reached
native NtContinue, which returned STATUS_NOT_IMPLEMENTED before Wine hit a
breakpoint. `nx-wow64-dynarec-2` implements cooperative ARM64 continuation
through the existing Horizon restore routine, including SIMD/FP control and
the active TEB. `[CONTINUE]` traces record the destination PC/SP. The routine
uses x17 as branch scratch; precise asynchronous context restoration remains
outside this change. `tests/check_continue_context.py` checks the context
conversion and x18 policy; actual continuation needs hardware validation.
The new package is `build-switch-wow64-dynarec/wine-nx-notepad-dynarec-2.zip`.

This directory contains the Switch runtime build/package tooling.

The main project README is at the repository root:

```text
../README.md
```

Common build command:

```sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

Staged SD package:

```text
wine-nx-probe/build-switch/sd-card/switch/wine
```

## WoW64 worker wait checkpoint

`nx-wow64-threads-2` fixes immediate timeouts in the Horizon select handler.
Pending waits poll at 1 ms intervals outside the object lock, using Wine's
monotonic or NT wall-clock deadline as appropriate. Infinite waits remain
pending; signal-and-wait signals once; wait-any returns the selected index.
This is a basic blocking implementation, not full APC, keyed-event, mutex
ownership/abandonment, timer, or thread-exit support.

Run `python3 wine-nx-probe/tests/check_select_wait.py` for host regression
coverage. The Switch package launches `pe32-threads.exe`; expected success is
both worker results zero, protected counter `0x40`, `PASS ALL`, and process
exit `0x2a`. Hardware confirmed on 2026-09-10: time, heap, single-thread
TLS, file I/O, both workers, TLS isolation, events, and the protected counter
all passed, ending with `PASS ALL` and exit `0x2a`. Workers deliberately
remain parked; normal thread return/exit and join are not yet validated.

## Experimental ARM64 dynarec checkpoint

Run `sh wine-nx-probe/check-box64-execution.sh` to build and test both CPU
backends in the ARM64 container and build their Switch NROs. The dynarec
artifact is `build-box64-core/switch/box64-execution-dynarec.nro`.
The normal Wine runtime still selects the interpreter.

The dynarec uses distinct writable/executable aliases, revalidates guest code
on each block entry, and cancels an interrupted translation before unwinding
its stack. Call/return patching stays disabled because it writes executable
code in place. Tests check real native dispatch, gate round trips, FS, SSE,
x87, reentry, repeated operand/decoder fault recovery, and changed code at the
same guest address. The pinned vendor checkout remains untouched; CMake
creates checked patched copies for the host integration.

Remaining limitations before runtime adoption: generated blocks do not enforce
instruction budgets; faults do not reconstruct precise guest registers from
native registers; writes inside an executing block are not trapped; the code
arena uses bounded bump allocation without reclamation; concurrent guest code
modification is not synchronized. The dynarec test does not claim instruction
count accuracy or bounded execution. The standalone hardware test exercises
split mappings, cache maintenance and fault unwinding; preservation of a real
Wine TEB in x18 still needs validation through the Wine runtime.

`nx-dynarec-2` routes RDTSC through the shared host timer helper instead of
emitting CNTVCT_EL0 reads. This addresses the suspected timer trap from the
first hardware run (`ESR=0x6244f821`). Both backends must now call the helper
exactly twice in the RDTSC regression. Hardware confirmed on 2026-09-11:
initial EAX=101, ECX=77, timer helper PASS, x87/CPUID/MMX PASS, six recovered
operand/decoder faults followed by successful atomic execution, 25 native
dispatch entries and 14,000 emitted bytes. Gates, FS, SSE, reentry and code
revalidation all passed. Zero reported instructions/checked reads reflects
missing dynarec instruction accounting, not a failure to execute guest code.

Next integration milestone: an opt-in dynarec Wine runtime running the existing
PE32 regression suite, retaining the interpreter default/fallback. Native
execution through the full Wine loader, WoW64 gates and real thread TEBs needs
its own hardware validation; the standalone pass does not establish that.

## Opt-in 7zr dynarec runtime

`sh wine-nx-probe/build-wow64-dynarec.sh` builds the normal interpreter package,
then the opt-in `WINE_NX_BOX64_DYNAREC=ON` runtime in
`build-switch-wow64-dynarec`. It stages the same validated DLLs, 7zr executable,
archive fixtures and two-thread benchmark arguments, and writes
`build-switch-wow64-dynarec/wine-nx-dynarec-1.zip`.

The runtime build label is `nx-wow64-dynarec-1`. `[DYNAREC]` logs native dispatch
entries and emitted bytes every five seconds; these are not guest instruction
counts. The benchmark should produce Avr:/Tot: rows, its lifecycle verdict,
and exit zero. The full Wine 7zr benchmark is hardware-confirmed below; broader application
compatibility and archive operations remain to be tested.
The default runtime option remains the interpreter. Failure to allocate the
initial code arena falls back to interpretation. Both SD packages target the
same `/switch/wine` directory, so installing the interpreter package restores
that runtime.

The package keeps the PE32 functional, worker and lifecycle tests available.
For the existing archive checks, change args.txt to `C:\7zr.exe t
C:\7zr-sample.7z` (on one line), or `C:\7zr.exe x C:\7zr-tree.7z
-oC:\7zr-out -y`. Do not infer archive correctness or a performance improvement
from a successful build; both need the next hardware run.

### Full Wine dynarec hardware result

The uploaded `debug/wine-nx-runtime.log`, `debug/stdout.txt` and
`debug/horizon-trace.log` confirm `nx-wow64-dynarec-1` completed
`C:\7zr.exe b 1 -mmt2 -md18` on Switch. Compression: 77 KiB/s;
decompression: 49,353 KiB/s; total benchmark rating: 2,030 MIPS.
Runtime telemetry: 9,053 native dispatch entries, 2,112,688 emitted bytes.
Thread lifecycle verdict PASS (8 exits, 7 reaped, one permitted pending
zombie, zero live engines); process exit code zero.

The CPUID brand still says "Wine-NX Box64 i386 interpreter" because it is a
static string in wow64_box64_engine.c; native-entry telemetry establishes
that this run used the dynarec. These benchmark results do not establish a
speedup without a matching interpreter measurement. Real archive creation,
integrity testing and extraction remain separate dynarec validation steps.
