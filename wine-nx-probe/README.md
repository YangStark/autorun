# Wine-NX Probe And Runtime

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
