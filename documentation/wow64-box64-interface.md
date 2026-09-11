# Native Wine / Box64 WoW64 interface

Implementation tree: wine-nx. Box64 is the x86 execution engine; ARM64 Wine
handles Windows services. The sibling's x64 Linux syscall emulator is reference
code, not the ABI between these components.

## Current implementation

- `wine-nx-probe/source/wow64_box64_bridge.c` implements the two guest-to-native
  gate transitions using Wine's actual `I386_CONTEXT` type. It is included in the
  native runtime build. The caller supplies checked guest-memory reads and
  native service callbacks. This is a transition bridge, not an x86 interpreter.
- `wine-nx-probe/source/wow64_box64_engine.c` now attaches the pinned Box64
  interpreter. It executes real i386 instructions, stops before either gate,
  exports integer/segment/SSE state, dispatches, and imports callback changes
  before resuming. Each invocation owns its engine state and supports nested
  entry. Deferred EFLAGS are materialized on return, FS uses the supplied TEB32
  base, and native floating-point state is restored before native callbacks.
- `dlls/ntdll/unix/signal_arm64.c` now implements current-thread i386 context
  get/set in the Switch branch. The shared helper preserves unrequested register
  groups, transfers integer/control/segment/debug/x87/FXSAVE state, and sets
  `WOW64_CPURESERVED_FLAG_RESET_STATE` for control changes. It explicitly rejects
  XSAVE extensions; non-current thread handles remain unsupported.
- `dlls/winebox64` supplies an opt-in ARM64 CPU DLL and a static native Unix
  table. It has not been selected as the default backend. The runtime accepts PE32 only in the opt-in
  interpreter build. No general software-emulator capability is advertised.
  The bridge-only tests use synthetic stacks. Separate execution tests now run
  real x86 bytes through Box64 in an ARM64 Linux container and cross-link the
  same test into a Switch NRO. The user confirmed the original gate/FS/SSE/reentry
  test passed on hardware (EAX=101, ECX=77, 14 instructions). New fault tests and
  full PE32 startup have not yet been confirmed on hardware.

## Entry and ownership

`dlls/wow64/syscall.c:process_init` obtains the CPU DLL name using
`get_cpu_dll_name()`. For i386 on native ARM64 its current default is
`libwow64fex.dll`; a registry override is supported. Implement a dedicated ARM64
PE CPU module (proposed name `winebox64.dll`) and select it only after it has a
working execution engine. Do not replace x64 `wow64cpu.dll`, whose assembly
performs real x86-64 mode switches.

The authoritative guest context lives in the current thread's CPU area,
referenced by `NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED]`.
`RtlWow64GetCpuAreaInfo` determines the aligned context location; do not assume
the layout for another guest machine. The engine must keep one state per thread
and support nested guest entry during native callbacks. FS must use the 32-bit
TEB base; an x86 selector value alone does not establish that base on ARM64.

The CPU DLL remains native ARM64 PE code. Box64/libnx code remains native NRO
code. Register a static Unix-call table for the CPU DLL, following
`virtual.c:wine_nx_static_unix_libs`; dispatch through the existing ARM64 PE/Unix
trampoline to preserve native ABI and x18 (Wine TEB). The current static resolver
only handles `!wow`; guest i386 Unix calls need actual WoW64 table registration
and structure conversion, not the ARM64 table reused blindly.

## CPU DLL contract traced from this tree

| Export | Required behavior |
| --- | --- |
| `BTCpuProcessInit` | Initialize core, allocate distinct guest-addressable gates, check mapping/engine support; set `WOW64_CPUFLAGS_SOFTWARE` only when implemented |
| `BTCpuThreadInit` | Set up per-thread engine state, guest FS/TEB and nested-entry storage |
| `BTCpuGetBopCode` | Return a stable address below 4 GiB for the syscall gate; Wine narrows it to ULONG |
| `__wine_get_unix_opcode` | Return a separate address below 4 GiB for the guest Unix-call gate |
| `BTCpuSimulate` | Import current i386 context, execute until a gate/fault, export complete state, dispatch and reimport any modified state |
| `BTCpuGetContext` / `BTCpuSetContext` | Use Wine's WoW64 context APIs; synchronizing another thread remains a separate requirement |
| `BTCpuResetToConsistentState` | Recover guest PC/register state from an actual engine fault and distinguish native faults; never report success with stale state |
| `BTCpuIsProcessorFeaturePresent` | Advertise only x86 features implemented by the selected core; do not forward ARM64 host features |
| `BTCpuSuspendLocalThread` | Coordinate engine quiescence/context visibility before exposing suspension support |
| Flush / memory notification exports | Invalidate translated blocks and synchronize memory changes; no success-only stubs |

The loader calls ProcessInit via a void function pointer and ignores its return
value. Initialization cannot report failure merely by returning an NTSTATUS.
It must terminate/raise through a supported Wine path before simulation starts.
`cpu_simulate` loops over BTCpuSimulate, so repeatedly returning without executing
or raising would hang the process.

## Exact gate ABI implemented

Source: `dlls/wow64cpu/cpu.c:syscall_32to64` and `unix_call_32to64`.
The addresses identify gates; the engine must stop before executing them.
Do not intercept every INT3 or arbitrary guest opcode as a native call.

| Value | Windows syscall gate | Unix-call gate |
| --- | --- | --- |
| Incoming service | EAX, including service-table selector bits | 32-bit index at ESP+12 |
| Handle | none | 64-bit value at ESP+4 |
| Arguments | guest address ESP+8 | 32-bit guest pointer at ESP+16 |
| Resume EIP | DWORD at ESP | DWORD at ESP |
| Resume ESP | ESP+4 (stub handles its own remaining stack) | ESP+20 |
| Result | EAX = service NTSTATUS | EAX = Unix NTSTATUS |

The bridge publishes resume EIP/ESP **before** calling native code. A callback,
exception operation or NtContinue can change the same Wine context. Afterwards,
only EAX receives the service result; the bridge does not overwrite modified
EIP/ESP or other registers with a stale pre-call snapshot. The engine must import
the resulting context again before resuming. The bridge retains no global or
thread-local cached context and allows recursive native dispatch.

The memory-read callback must validate/copy guest stack words and report faults.
Overflow, unknown gates, unavailable callbacks and read failures do not consume
guest state. Native service errors are ordinary results in EAX, not bridge
failures. The Unix-call callback must validate handles and indices against known
WoW64 tables; guest data cannot become an arbitrary native function pointer.
Native argument access still needs the runtime's real exception boundary.

## Engine adapter status and remaining integration

`cmake/Box64Core.cmake` builds execution sources at
`dae0917c47b4edd8956f314210417a20fd225c4b`. It generates an interpreter copy with
one before-fetch hook; the upstream checkout stays clean. No ELF loader, Linux
syscall implementation, library wrapper set, or dynarec is linked. Box64's MIT
license is retained in the pinned vendor checkout; the adapter is LGPL-2.1-or-later.
`bootstrap-box64-core.sh` fetches the pinned revision and refuses modified or
different existing checkouts. The build reserves ARM64 x18 for Wine's TEB.

The execution tests now compute 42 in i386 code using an FS-relative load,
transfer it through XMM0 and the Windows service gate, receive a native result,
observe callback changes to EBX/XMM0 and write 101 to guest memory. A nested
interpreter invocation returns 11. A second x86 program exercises the Unix gate's
64-bit handle and 20-byte stack cleanup. Both gates contain non-executable trap
bytes and are intercepted by address before fetch. Loop-budget exhaustion,
unsupported x87 instructions, Linux INT 80h rejection and invalid instruction
fetches are also tested. The instruction budget counts main interpreter
instructions; a long REP operation is not a wall-clock timeout.

This is deliberately an experimental core entry, not a complete CPU backend:

- The adapter accepts empty x87 state and default MXCSR (`0x1f80`). x87/AVX
  instruction families and XSAVE contexts are rejected; full x87/MMX conversion,
  exception/status semantics and wider SIMD state remain unfinished. Do not
  advertise comprehensive SSE or MMX support based on register-transfer tests.
- CPUID, RDTSC, random-instruction and Linux syscall helper paths currently stop
  with explicit unsupported status rather than invoking host syscalls or
  inventing CPU capabilities. Normal guest INT3 is a breakpoint, not a gate.
- Instruction-entry reads and bridge stacks are checked. Direct operand faults
  now unwind the active interpreter and release its tracked atomic mutex.
  Linux tests cover five real faults and successful subsequent atomic execution;
  the Horizon handler has the same fail-stop boundary. This reports failure,
  not resumable x86 SEH. The updated fault-test NRO still needs hardware testing.
- Remote suspension and general SEH support have not been added by this core
  milestone. Thread lifecycle is covered below (lifecycle-1, awaiting hardware).

`dlls/winebox64` now supplies the ARM64 PE CPU DLL (opt-in configure option
`--enable-winebox64=aarch64`). Its versioned Unix call enters the statically linked
interpreter table, returning at a gate with the x86 stack and EAX intact. The PE
side dispatches `Wow64SystemServiceEx`, then resumes from Wine's current CPU area.
Gates use Wine's address-mask form of ZeroBits (`0x7fffffff`), not a bit count of 32.
Failed initialization terminates explicitly. Feature reporting is conservative;
suspension and exception reset return unsupported. Guest Unix calls are restricted to the
registered ntdll WoW64 table with handle and index validation.

`sh wine-nx-probe/build-wow64-components.sh` builds native CPU/wow64/wow64win/ntdll,
i386 ntdll, a minimal relocatable PE32 and the Switch runtime with the engine
linked. It requires a configured `build-wine-wow64-pe` tree with
`--enable-archs=aarch64,i386 --enable-winebox64=aarch64` and the LLVM-MinGW toolchain
specified in the root README. Outputs are staged under
`build-switch-wow64/sd-card/switch/wine`, including ARM64 native DLLs, i386
ntdll/kernel32/kernelbase, NLS data, the NRO, and launch files. The PE32 imports
only NtQuerySystemTime and NtTerminateProcess and expects exit 42 (`0x2a`).
`tools/verify-wow64-package.py` checks architectures, direct imports, CPU exports,
relocations and launch files.

The opt-in runtime detects the process machine before mapping; initializes Wine's
TEB32/PEB32, process parameters, stack and CPU area; loads native WoW64 modules;
sets both ntdll initialization blocks; and enters Wow64LdrpInitialize. Wine's
x86 LdrInitializeThunk owns guest imports and TLS. No direct call to an x86 entry
point is made from ARM64. Full PE32 startup remains unverified on hardware and
may stop on unsupported instructions or remaining native Wine services. Failure
logs include the engine's status/EIP and native startup stages. Native ARM64
runtime build remains the regression baseline; Notepad was not rerun here.

## Verification

Run from the repository root:

```sh
sh wine-nx-probe/check-wow64-box64-bridge.sh
sh wine-nx-probe/check-box64-execution.sh
```

The host tests use this repository's Wine headers and AddressSanitizer/UBSan.
They cover syscall argument offsets and win32u selector bits, 64-bit Unix handles,
Unix stdcall cleanup, native error propagation, context redirection, nested
dispatch, bad stack reads, address wraparound, unknown/missing gates, partial
context transfers and unsupported XSAVE/machine cases.

The first command runs the original host ABI tests with AddressSanitizer/UBSan.
The second fetches Box64, runs real execution tests in the ARM64
`devkitpro/devkita64` container, and builds the Switch smoke NRO. Both test suites
pass, and the Switch core/adapter/test compile and link successfully.

Artifact: `wine-nx-probe/build-box64-core/switch/box64-execution.nro`.
This NRO executes the test sequence, not Wine or a PE file. It attempts a fixed
64 KiB guest arena at `0x10000000` using AliasCode mapping; if that address is
unavailable it reports failure without running the sequence. It does not change
the launcher configuration. Use title override. Hardware execution is unverified.

The main runtime has `WINE_NX_BOX64_INTERPRETER=OFF` by default. Setting it ON
compiles/links the core adapter but does not select a CPU DLL or launch PE32.
The full Wine runtime/package was not rebuilt as part of this isolated test.

## Hardware report

The user reported a Switch run of `box64-execution.nro` with status 00000000,
EAX 101, ECX 77, 14 instructions and the passing FS/SSE/reentry/budget message.
This confirms that interpreter smoke on hardware; it does not establish PE32
Wine startup. The later DLL-boundary stop/resume case still needs its own device run.

## Hardware startup checkpoint: bootstrap 2

The first full runtime log confirmed the PE32 image at `0x10000000`, PEB32
initialization and native main-module registration. It then returned
`c0000002` (`STATUS_NOT_IMPLEMENTED`): the bootstrap attempted `NtCreateKey`
to choose the CPU DLL, but Horizon has no create-key request handler.

Build `nx-wow64-bootstrap-2` removes that registry write. The native loader sets
the private ARM64 wow64 export `__wine_switch_cpu_backend` to I386 before
`Wow64LdrpInitialize`. Zero retains normal Wine CPU selection; the explicit
Switch setting chooses `winebox64.dll`. Package validation requires this export,
so the NRO and rebuilt `wow64.dll` must be installed together. Startup now logs
native bootstrap and each native module load separately. This correction is
build-verified; the next hardware checkpoint is native WoW64 DLL loading.

## Hardware success and combined functional test

The supplied bootstrap-2 hardware logs end with
`[EXIT] NtTerminateProcess(self) exit_code=0x0000002a`. They confirm native WoW64
DLL initialization, x86 ntdll/kernel32/kernelbase loading, and the minimal PE32
program's successful time syscall and termination. Parking afterward is expected.

The package now targets `pe32-functional.exe` using build
`nx-wow64-functional-1`. This CRT-free x86 program runs all four groups in one
launch: time query; process/private heaps, zeroing and reallocation; 80 dynamic
TLS slots including expansion, freeing and clean reuse on the current thread;
and file write/flush/close/reopen/read/EOF/seek/delete with byte comparison.
It uses only existing kernel32/ntdll dependencies. It does not test worker-thread
isolation, compiler static TLS, GUI, x87 or game compatibility.

Each group emits BEGIN and PASS/FAIL plus a detail code via NtDisplayString,
now routed to the Switch runtime log. A failed check does not skip other groups.
`PASS ALL` and exit `0x2a` mean success; failure exits `0x100 | mask`, with bits
1=time, 2=heap, 4=TLS and 8=file I/O. A native fault may still stop the process;
the last BEGIN identifies the active group. File testing creates and removes
`C:\wine-nx-functional-test.tmp`. The new combined test is build-verified, awaiting
hardware results. Source: `wine-nx-probe/tests/pe32_functional.c`.

## Functional hardware checkpoint and file-access fix

The functional-1 hardware log passes time, process/private heaps and 80 TLS
slots including expansion and reuse. File I/O fails at detail 2 (first write):
NtCreateFile succeeds with access `0x40100080`, then NtWriteFile returns
`c0000022` (access denied). Final mask `0x8`/exit `0x108` isolates file I/O.

Horizon previously passed generic access bits directly to descriptor-mode
selection and the stored file rights. Functional-2 maps GENERIC_READ/WRITE/
EXECUTE/ALL with Wine's file generic mapping before both operations. Ordinary
GENERIC_WRITE no longer implies append-only behavior: O_APPEND is used only
when FILE_APPEND_DATA is present without FILE_WRITE_DATA.

`tests/horizon_file_access.c` validates masks against Wine constants and uses
real host files to exercise write, overwrite after seeking, append-only and
read-only rejection. ASan/UBSan pass. Hardware verification of the corrected
full file group remains pending. Run the same combined test again.

## Functional-2 hardware checkpoint: deletion

Time, heaps and TLS still pass. File detail `0x0f` means write/flush/reopen/
read/EOF/seek all succeeded, and DeleteFileW returned success, but the file was
still present. Horizon ignored FILE_DELETE_ON_CLOSE (`0x1000`). Functional-3
removes the path when the last handle referencing that file object closes,
checks DELETE access at open, and propagates unlink/rmdir errors from close.
Normal closes do not remove files. This does not yet implement complete Windows
sharing/delete-pending coordination across separately opened file objects.

`python3 wine-nx-probe/tests/check_delete_on_close.py` extracts the actual close
handler and exercises it with host files: normal close, two duplicate references,
final-close deletion, failure propagation and directory deletion. ASan/UBSan pass.
The packaged combined test remains unchanged; hardware deletion is pending.

## Worker-thread milestone (threads-1)

Functional-3 hardware still passes time/heaps/TLS and all file operations before
deletion. Final deletion now returns an error instead of falsely succeeding:
Horizon reports `c000000d`. The object descriptor was still open at unlink time.
The close path now closes it first and logs raw errno alongside NTSTATUS. The
host close-handler test also asserts the descriptor is closed before unlink.
Hardware verification remains pending.

`nx-wow64-threads-1` routes new I386 thread entries through the existing native
Wow64LdrpInitialize and a fresh Wine CPU context with each worker's TEB32/stack.
It never directly branches to x86 code as ARM64. Suspended starts are not supported.
The new `pe32-threads.exe` retains all previous functional groups and adds two
workers: initially clear TLS, distinct TLS values preserved across mutex waits,
main-thread TLS unchanged, manual-reset start event, auto-reset completion events,
and a mutex-protected counter expected to reach 64. Waits have 10-second bounds.
Workers signal completion then wait indefinitely, so thread termination/join,
static TLS/DLL lifecycle completeness, and suspension are not claimed as tested.
Failure mask adds bit 16 for workers. Hardware run is required for this milestone;
CPU feature expansion and GUI remain subsequent work.

## Worker-thread hardware checkpoint (threads-2)

The `nx-wow64-threads-2` hardware log passes every group: time, heaps, 80 TLS
slots, file I/O including deletion (`[HZFILE] delete result=0`), and two
workers with isolated TLS, events and a mutex counter of 64. It ends with
`PASS ALL` and `exit_code=0x0000002a`. Blank lines in that log were two threads
interleaving one message and its newline; lines are now written atomically.

## Thread lifecycle milestone (lifecycle-1)

Build `nx-wow64-lifecycle-1` completes the x86 thread lifecycle: workers exit by
returning or through `ExitThread`, joins wait for the real end, exit codes are
reported, and each thread's resources are reclaimed. Before it, thread objects
were permanently signaled and the in-process server had no `terminate_thread`
or `get_thread_info`. Consequently `RtlExitUserThread` read an uninitialized
`ThreadAmILastThread` result, and failed `NtTerminateThread` calls looped forever.

Thread objects (`dlls/ntdll/unix/horizon.c`, state in `horizon_threads.h`):

- A thread object is referenced by its handles and by its server connection.
  It becomes signaled when that connection sees the request pipe close.
  `exit_thread` closes that pipe last, after `LdrShutdownThread`, TLS callbacks
  and every Windows frame have finished. `terminate_thread` on the caller only
  records the exit code.
- `get_thread_info`, `get_thread_times`, `set_thread_info` and `open_thread`
  (by id) are implemented. The current-thread pseudo-handle resolves per
  connection, and duplicating it yields a real handle. The main thread uses
  tid 4 from the same counter as workers (8, 12, ...); a kernel-derived id could
  collide with a worker id or its `NtWaitForAlertByThreadId` slot.
- `CREATE_SUSPENDED` is honoured at a start gate: `init_thread` is not answered
  until `resume_thread` releases it, so no Windows code runs first. Wine's
  kernelbase creates every thread suspended and resumes it afterwards. Suspending
  a running thread and terminating another thread need an interpreter-safe stop
  point. Both return `STATUS_NOT_SUPPORTED`, logged via `horizon_trace`.
- Mutexes track their owner. They are recursive, `ReleaseMutex` by another
  thread fails, and a mutex held by an exiting thread is abandoned. The next
  waiter receives `WAIT_ABANDONED`.
- libnx cannot detach threads (`pthread_detach` returns `ENOSYS`), and a
  thread's kernel object, stack and stack mapping persist until `pthread_join`.
  Each connection pthread queues itself when it ends. `new_thread` and the next
  ending connection join it. Worker pthreads are joined by Wine's `exit_thread`,
  one exit behind, which also frees the previous TEB and its stacks. Horizon
  gives an application about 96 kernel threads, so any leak fails quickly.

Synchronization (`dlls/ntdll/unix/sync.c`):

- `NtWaitForAlertByThreadId` returned immediately on Switch, so contended
  critical sections, SRW locks and condition variables busy-spun. It now uses
  the futex path with Horizon address arbitration (`svcWaitForAddress`,
  `svcSignalToAddress`).
- Keyed events were also immediate. `RtlRunOnce` (INIT_ONCE) links waiters
  through their own stacks, so a completer could walk a dead or self-linked list.
  Keyed events now pair waits and releases in-process, each side blocking until
  its peer arrives.
- In the WoW64 path the main thread drops from hbloader's priority to 59. Horizon
  round-robins only priority 59 on cores 0-2, every 10 ms, and libnx creates
  every worker at 59. A higher-priority spinner could otherwise starve a lock
  holder on its core.

Native ARM64 threads call their procedure directly (without the loader's thread
attach). When it returns they now exit through `NtTerminateThread` with its
value instead of parking, because joins on them would otherwise never return.

Accounting: the runtime logs `[LIFECYCLE] exit` for every thread exit and
`[LIFECYCLE] final` at process exit. The final line counts running threads, thread
objects, connections, zombie connection threads, pipes, TEBs, unjoined worker
pthreads and live Box64 engines. `[LIFECYCLE] verdict=PASS` requires every count
to return to the baseline taken before the first thread was created. Only one
exited thread's TEB and pthread may linger (freed by the next exit), and no
interpreter engine may remain. Engines exist only during a run and are freed
before any gate is dispatched, so an exiting thread owns none.

`pe32-lifecycle.exe` (`tests/pe32_lifecycle.c`) is the acceptance test. After the
functional groups it checks:

- exit: STILL_ACTIVE and non-signaled live threads (also a 50 ms timed wait);
  return versus nested `ExitThread` with a full 32-bit code; a join that waits
  past a worker's final sleep; wait-any index and wait-all; `OpenThread` after
  the creator closed its handle; `GetThreadTimes`; a duplicated
  `GetCurrentThread`; abandoned and recursive mutexes; TLS callback
  attach/detach counts.
- suspended: `CREATE_SUSPENDED`, nested `SuspendThread`/`ResumeThread` counts,
  and that no code runs before the last resume.
- rounds (default 48 x 4 workers): static TLS (a hand-written TLS directory with
  a callback), dynamic TLS, critical sections, SRW locks, a condition-variable
  barrier, recursive kernel mutexes, a semaphore and INIT_ONCE. Joins alternate
  between `WaitForSingleObject` and `WaitForMultipleObjects`. Exit codes, counters
  and TLS isolation are verified. `C:\pe32-lifecycle.exe rounds=N` in `args.txt`
  changes the count.
- reuse: worker TEB addresses and 32-bit stack bases must stay few (at most 24
  distinct values). Recycled memory repeats addresses; leaked memory does not.

Success is `[PE32 TEST] PASS ALL`, `[LIFECYCLE] verdict=PASS` and
`exit_code=0x0000002a`. Failure exits `0x100 | mask`
(1=time 2=heap 4=TLS 8=file 16=exit 32=suspended 64=rounds 128=reuse), and the
first failing detail code is logged. Round failures encode `round << 8 | check`.
The log is buffered and flushed every 200 ms, plus immediately for test,
lifecycle-verdict, fault and exit lines. `[SYSCALL]` lines no longer go to the
on-screen console.

Host verification: `tests/horizon_threads.c` covers the thread and mutex state
machines, keyed-event ordering (a waiter before and after its release, key and
object isolation, timeouts, 16-way pairing) and joining 200 self-queued threads.
It runs under ASan/UBSan and TSan. `check_select_wait.py` covers abandoned
wait-any/wait-all statuses. Compile-time asserts compare every new request and
reply struct with `server_protocol.h`. The WoW64 package and an
interpreter-off native runtime build and link, and the package verifier requires the test's
imports and TLS directory. Hardware verification is pending.

Known limits: suspending or terminating another running thread is unsupported.
libnx handles every fault on one global exception stack, and
`svcReturnFromException` releases the kernel's per-process serialization before
the handler runs. Faults on two threads at the same time are therefore unsafe;
Wine commits thread stacks up front, so normal execution does not fault.
Server waits still poll every millisecond.

## Lifecycle hardware checkpoint and suspension fix (lifecycle-2)

The `nx-wow64-lifecycle-1` hardware run passed the functional groups, static TLS
and the whole exit-semantics group. That covers nine workers with the exact
return and `ExitThread` codes (`0x1007`, `0xe0000042`), a join that waited past
a worker's final sleep, wait-any/wait-all, `OpenThread`, the duplicated
`GetCurrentThread`, abandoned and recursive mutexes, and TLS callbacks
attached/detached 9/9. The `[LIFECYCLE] exit` lines showed each exited
thread's TEB, pthread, pipes and connection thread being reclaimed. Only four
TEB slots were committed for ten threads.

It then faulted at PC 0 in the PE ntdll (`lr` inside `RtlWow64SuspendThread`)
on the suspended-start group's first `SuspendThread`. Upstream `init_wow64()`
resolves `Wow64LdrpInitialize`, `Wow64PrepareForException` and
`Wow64SuspendLocalThread` from wow64.dll. The Switch bootstrap enters
`Wow64LdrpInitialize` directly, so the PE ntdll's other two pointers stayed
null. `RtlWow64SuspendThread` calls `pWow64SuspendLocalThread` unconditionally
for same-process threads. The runtime compiles its own copy of `loader.c`,
so setting its globals is not enough. PE ntdll now exports both pointers
privately (like `wine_nx_pe_hash_table`), the bootstrap fills them and stops
with `STATUS_PROCEDURE_NOT_FOUND` if they are missing, and the package verifier
requires the exports. winebox64's `BTCpuSuspendLocalThread` now calls
`NtSuspendThread`, so the server suspends only threads held at the start gate
and refuses running threads with `STATUS_NOT_SUPPORTED`. Build
`nx-wow64-lifecycle-2` reruns the same test; hardware verification is pending.

## Lifecycle hardware result (lifecycle-2): passed

The `nx-wow64-lifecycle-2` hardware run passed every group and ended with
`[PE32 TEST] PASS ALL`, `[LIFECYCLE] verdict=PASS` and `exit_code=0x0000002a`.

- 202 x86 threads exited: 9 exit-semantics workers, 1 suspended-start worker,
  and 48 rounds of 4 synchronized workers. Every exit code matched.
- `CREATE_SUSPENDED` held its thread until the second `ResumeThread`: the first
  guest instruction appears only after it. `SuspendThread` on the held thread
  worked through the restored `pWow64SuspendLocalThread` hook.
- Reclamation, from the final line: running=1, thread objects=1, connections=1,
  pipes=4 (all at baseline), one lingering TEB and pthread, 201 connection
  threads joined, no live engines. Peak counts during the rounds were 5 threads,
  16 pipes, 4 TEBs and 2 engines, with no growth from round 1 to round 48.
  Workers reused 5 distinct TEBs and 5 distinct stacks.

Gap found: `GetTickCount` never advanced (the test's elapsed times read 0).
kernelbase reads `KUSER_SHARED_DATA.TickCount`, which wineserver refreshes
continuously, but the in-process Horizon server never updates it. The lifecycle
checks use real wait timeouts and were unaffected; real applications need it
fixed.

## Real console application milestone (console-1)

Target: 7-Zip 26.03 `7zr.exe`, the vendor's 32-bit x86 console build
(`samples/7zr-x86`, SHA-256 recorded there). It is MSVC-built, relocatable
(preferred base `0x400000`, below Horizon's mappable range) and imports
KERNEL32, USER32, ADVAPI32, OLEAUT32 and the dynamic MSVCRT. Its load-time
closure is 15 i386 Wine DLLs: advapi32, combase, coml2, gdi32, kernel32,
kernelbase, msvcrt, ntdll, ole32, oleaut32, rpcrt4, sechost, ucrtbase, user32
and win32u. Delay-loaded DLLs are not staged. The package runs
`C:\7zr.exe b 1 -mmt2 -md18`: one LZMA benchmark pass, 2 threads, 256 KiB
dictionary. 7-Zip compresses, decompresses and CRC-checks its own data.

Changes:

- Box64 adapter: x87 and MMX instructions now execute. Their state, with SSE,
  crosses runs in the context's FXSAVE area, in the layout Box64 uses for the
  guest's own `FXSAVE`/`FXRSTOR` (doubles, TOP-relative tags); FXRSTOR's missing
  push counter is recomputed. `FloatSave` receives an architectural FNSAVE image
  (80-bit registers, physical tags) for readers and is not read back.
- CPUID reports a conservative Pentium 4 class CPU: FPU, TSC, CX8, CMOV, MMX,
  FXSR, SSE and SSE2, vendor GenuineIntel, and brand
  "Wine-NX Box64 i386 interpreter". Nothing needing unsupported state, such as
  AVX, is advertised. RDTSC returns monotonic nanoseconds.
  `BTCpuIsProcessorFeaturePresent` reports the same set, since x86
  `IsProcessorFeaturePresent` goes through WoW64 to it.
- `KUSER_SHARED_DATA`: wineserver keeps its clock current; the in-process
  server did not, so `GetTickCount` read 0 (see lifecycle-2). The page is now
  writable on Switch. A thread refreshes SystemTime, InterruptTime and TickCount
  every millisecond, storing them in wineserver's order, and sets the static
  fields.
- Standard handles: Horizon has no console device. stdin, stdout and stderr are
  `sdmc:/switch/wine/std{in,out,err}.txt`, and at exit the runtime copies stdout
  and stderr into its log as `[STDOUT]`/`[STDERR]` lines. The environment now
  has PATH, SystemDrive, SystemRoot, TEMP, TMP and windir.
- Relocation: the x86 loader relocates the main image, because it is the
  PEB's `ImageBaseAddress`. The runtime now relocates the guest ntdll itself if
  it misses its base, as upstream `load_wow64_ntdll` does.

Host verification: the ARM64 container execution tests now run x87 code
through a gate both within one run and across the stop/free/reimport boundary
winebox64 uses. They check the control word, TOP, physical tags and exact
80-bit ST0/ST1, and that a change the callback makes to the FXSAVE area is
imported. They also cover CPUID features, RDTSC monotonicity and an MMX
register surviving the boundary. The package verifier checks load-time import
closures only (delay-loaded DLLs may be absent) and requires 7zr's imports. The
interpreter-off native runtime still builds. Hardware verification is pending.

Success: `[STDOUT]` lines with the benchmark table ending in `Tot:`, then
`exit_code=0x00000000`. The thread-lifecycle test remains staged as a
regression target.

Untested on hardware: user32, gdi32, ole32 and combase process attach through
WoW64, and msvcrt std-handle detection on Horizon files. x86 SEH for faults is
still unsupported.

## Console hardware checkpoint (console-1) and speed fix (console-2)

The `nx-wow64-console-1` run loaded 7zr's whole load-time closure: sechost,
rpcrt4, ucrtbase, msvcrt, user32, gdi32, win32u, combase and coml2. The
native win32u font system also initialized for the x86 user32/gdi32. user32
then looked for `imm32.dll` (loaded during its process attach, not a
load-time import) and continued without it. 7zr wrote its banner through 8
successful `NtWriteFile` calls, allocated benchmark buffers and started timing.
It was still computing when closed. One benign fault appeared: a
`PAGE_EXECUTE_READWRITE` allocation was mapped without write access, and
Wine's fault handler switched it to RW on the first store.

Two problems followed:

- Output was copied into the log only at exit, so a slow or stopped run showed
  none. The log flusher now echoes new stdout/stderr lines every 200 ms.
- The interpreter hook read every opcode and prefix byte through
  `NtReadVirtualMemory`, plus one more in `CheckExec`: at least two calls per
  x86 instruction. On Horizon each call is two Wine `__TRY` frames (setjmp,
  exception-frame push) and a buffer probe. The first fetch from each code page
  still uses the checked read, and later bytes are read directly. A page
  unmapped meanwhile faults inside Run and unwinds through the operand-fault
  boundary. A host test runs 20,003 instructions with one checked read, and all
  fault-recovery tests still pass.

Build `nx-wow64-console-2` also logs `[BOX64] instructions=... (M/s)` every
5 seconds, the first measurement of interpreter speed on hardware. It no
longer traces the `NtReadVirtualMemory` syscall winebox64 issues for each x86
syscall (about three quarters of the previous log). It stages imm32 and makes
the default workload deterministic and bounded: `7zr t` on
`samples/7zr-x86/7zr-sample.7z`. That archive is LZMA2 with two known files,
325 KB unpacked, both CRC-checked. Success: `[STDOUT] Everything is Ok`,
`Files: 2`, `exit_code=0x00000000`. The benchmark remains available through
`args.txt`. Hardware verification is pending.

## Console hardware result (console-2) and output capture (console-3)

The `nx-wow64-console-2` run of `7zr t C:\7zr-sample.7z` completed with
`exit_code=0x00000000`. The interpreter executed 23,595,907 x86 instructions at
4.22 million per second in 451 runs (55 per second). 7zr scanned `C:\`, opened
and read the archive, allocated its LZMA buffers and made 16 successful
`NtWriteFile` calls. Exit code 0 is 7-Zip's no-error result; a data or CRC error
in test mode returns 2. The same benign write fault on a
`PAGE_EXECUTE_READWRITE` page appeared as in console-1. There were no other
faults and no missing load-time modules. advapi32 looked for `cryptbase.dll`,
which is not staged, and continued.

The log contained no `[STDOUT]` line. The console-2 pump re-read
`stdout.txt` from the SD card with `stat` and `fopen` while Wine's file object
still held it open for writing. Neither the 200 ms pump nor the final pump,
which also runs before the handles close, saw any data.

Build `nx-wow64-console-3` captures output where it is written instead:

- The runtime tags the server file objects behind the stdout and stderr handles
  (`horizon_mark_std_stream`). Because the tag is on the object, duplicated
  handles are included.
- After each successful write, `NtWriteFile` passes the bytes for a tagged
  object to the runtime (`horizon_echo_std_write`, then
  `wine_nx_runtime_std_write`). The files on the SD card are still written.
- `source/std_stream_lines.h` turns the bytes into `[STDOUT]`/`[STDERR]` lines.
  CR and LF end a line, and each CR-rewritten progress version is kept.
  Backspace moves the cursor left, so later text overwrites. Control
  characters are dropped, and lines over 511 characters are split. A partial
  line, such as a prompt, appears after one second idle. The log keeps 2000
  lines per stream.
- At exit, `[STDIO] STDOUT bytes=... lines=...; stat while open: ...` records
  how much each stream received. It also records what `stat` reports for a
  file that is still open for writing, which would explain console-2.

Host verification: `sh wine-nx-probe/check-runtime-console.sh` (AddressSanitizer
and UBSan) covers CRLF, output split across writes, CR progress, backspace
erasing, control characters, overlong lines and the idle flush. The thread and
wait host tests and the standalone Horizon syntax target still pass. The package
builds, and the verifier passes.

Success: `[STDOUT] Everything is Ok`, `[STDOUT] Files: 2`,
`[STDOUT] Size:       325536`, `[STDIO] STDOUT bytes=` nonzero, and
`exit_code=0x00000000`. Hardware verification is pending.

## Console-3 hardware result and directory information (console-4)

`nx-wow64-console-3` captured 7zr's complete output: 16 `[STDOUT]` lines,
350 bytes, byte for byte the `stdout.txt` left on the SD card. It printed
`Everything is Ok`, `Files: 2` and `Size: 325536`, then exited with
`exit_code=0x00000000`. The interpreter executed 23,595,907 instructions at
4.21 million per second, the same count as console-2. At exit, `stat` of both
standard files failed with `rc=-1 errno=5`. The SD card will not open a file a
second time while it is open for writing, and libnx reports the file system's
refusal as EIO.

Two lines differed from the same `7zr t` under Wine 11.0 on a Mac:
`1 file, 0 bytes` instead of `1 file, 85885 bytes (84 KiB)`, and
`Compressed: 0` instead of `Compressed: 85885`. Horizon directory handles have
no unix descriptor, so `NtQueryDirectoryFile` used Wine's server-side
directory path. That path returns names only ("Not filling attributes"). Every
entry came back with size 0, no times and no `FILE_ATTRIBUTE_DIRECTORY`, which
would also break recursive scans. The Id information classes returned
`STATUS_NOT_SUPPORTED`.

Changes:

- The Horizon server implements `get_handle_unix_name` (request 47), which
  returns a file object's unix name. Its number and layout were checked against
  `server_protocol.h`.
- On Switch, `NtQueryDirectoryFile` asks the server for the next matching name
  and stats that entry below the directory's unix name. Upstream's
  `get_dir_data_entry` then fills the result. That gives every directory
  information class sizes, times, attributes and upstream's truncation rules.
  Entries that disappear before they can be examined are skipped. Short names
  stay empty. The name-only server path is no longer compiled on Switch.
- Files open for writing: `get_file_info`, used by attribute queries and
  listings, and the runtime's `fstatat`, used by name lookup, fall back to
  `fstat` on the descriptor of an open server file object with the same unix
  name. Names are compared ignoring ASCII case and repeated or trailing slashes.
- libnx reports `st_dev` and `st_ino` as 0 for every file, so a single ignored
  path would have hidden every directory entry. `ignore_file` now skips files
  without an identity.
- At exit, the `[STDIO]` line also records the file system result code, and
  whether the open-file fallback works for `stdout.txt`.

Workload: `7zr a C:\wine-nx-tree.7z C:\7zr-tree -mx1`. The build stages
`C:\7zr-tree` with `tools/make-7zr-tree.py`: `readme.txt`,
`data\wine-nx-sample.bin` and `data\text\wine-nx-sample.txt`. The last two are
byte-identical to the payloads of `7zr-sample.7z`, confirmed by extracting that
archive on the host. The command needs the directory flag to recurse, entry
sizes for the scan summary, file reads, and a new archive written with seeks.
The same command under host Wine 11.0 prints:

```text
Scanning the drive:
3 folders, 3 files, 325691 bytes (319 KiB)

Creating archive: wine-nx-tree.7z

Add new data to archive: 3 folders, 3 files, 325691 bytes (319 KiB)

Files read from disk: 3
Archive size: 90149 bytes (89 KiB)
Everything is Ok
```

The archive size may differ slightly, because the stored timestamps are
compressed along with the headers. After this run, `C:\7zr.exe t
C:\wine-nx-tree.7z` checks the archive on the Switch. `t C:\7zr-sample.7z`
should now print `1 file, 85885 bytes (84 KiB)`.

Host verification: `check-runtime-console.sh` now also runs
`tests/horizon_file_access.c`. It covers open-file path matching and entry
names: device roots, trailing and doubled slashes, `.` and `..`. The package
verifier compares the staged tree byte for byte with the generator's output and
requires that no output archive is staged. The thread and wait host tests, the
standalone Horizon syntax target and the package build pass. The new code
compiles without warnings.

Limits: listing n entries costs O(n²) server reads, because each call reopens
the directory and skips to its position. Each entry also costs a `stat`, which
is several file-system IPC calls. A second `CreateFile` of a file that is open
for writing is expected to fail the same way `stat` does; it is untested.

Success: `[STDOUT] 3 folders, 3 files, 325691 bytes (319 KiB)`,
`[STDOUT] Files read from disk: 3`, `[STDOUT] Everything is Ok`, and
`exit_code=0x00000000`. Hardware verification is pending.

## Console-4 hardware result and directory handles (console-5)

`nx-wow64-console-4` ran `7zr a C:\wine-nx-tree.7z C:\7zr-tree -mx1`. It
created and wrote a valid empty 32-byte archive, but reported
`WARNING: File not found. C:\7zr-tree`, `0 files, 0 bytes`, and exit code 1
(warnings). The `[STDIO]` diagnostic recorded the file-system result for
`stat` of an open-for-writing file, `fs=0xe02` (module 2, description 7:
target locked), and `open-file stat=0`: the fallback works on hardware.

Sequence: `FindFirstFileW(C:\7zr-tree)` found the entry (server query
`mask=7zr-tree name=7zr-tree err=0`). 7-Zip then opened the directory with
`NtCreateFile` twice, access `0x100080` (`FILE_READ_ATTRIBUTES | SYNCHRONIZE`),
options `0x4020` (backup intent, no `FILE_DIRECTORY_FILE`). Both opens failed
with `STATUS_NO_SUCH_FILE`. Three problems contributed:

- Mount-point guess. Upstream `get_file_info` marks a directory as a
  mount-point reparse point when its inode equals its parent's. libnx reports
  inode 0 for every file, so on Switch every directory can match its parent. A
  reparse point makes 7-Zip resolve the item by opening it and calling
  `GetFileInformationByHandle`, which matches the two opens. On Switch the test
  now requires a real inode number, which also saves a `stat` of `..` per
  directory.
- Directory opens. The Horizon server treated a path as a directory only when
  `FILE_DIRECTORY_FILE` was set; otherwise it called `open()`, which libnx
  cannot do on a directory. After `open()` fails on a path `opendir()` accepts,
  the server now opens it as a directory object, following wineserver's
  `open_fd`: `FILE_NON_DIRECTORY_FILE` gives `STATUS_FILE_IS_A_DIRECTORY`, and
  a create or truncate disposition gives `STATUS_OBJECT_NAME_COLLISION`
  (`horizon_directory_open_status`). Ordinary file opens cost nothing extra.
- Information on directory handles. These handles have no descriptor, so
  `NtQueryInformationFile` fell back to the server's `get_file_info` request,
  which Horizon does not implement. On Switch, `fd_get_file_info` now examines
  a descriptor-less handle by its unix name. Basic, standard, stat, network,
  attribute-tag, internal and end-of-file information work for directories, and
  the position reads 0.

`7zr a` works with or without the empty archive console-4 left behind. Under
host Wine 11.0, updating that 32-byte archive prints `Open archive`,
`Updating archive` and the same summary as a fresh run:
`3 folders, 3 files, 325691 bytes (319 KiB)`, `Files read from disk: 3`,
`Archive size: 90149 bytes (89 KiB)`, `Everything is Ok`, exit 0. The update
also exercises the temporary file, delete and rename.

Host verification: `tests/horizon_file_access.c` checks the directory-open
decision against Wine's status constants. The package build, verifier,
standalone Horizon syntax target and thread and wait tests pass. The new lines
add no warnings.

Success: `[STDOUT] 3 folders, 3 files, 325691 bytes (319 KiB)`,
`[STDOUT] Files read from disk: 3`, `[STDOUT] Everything is Ok`, and
`exit_code=0x00000000`. Hardware verification is pending.

## Console-5 hardware result: archive created (and extraction, console-6)

`nx-wow64-console-5` passed on hardware. `7zr a C:\wine-nx-tree.7z C:\7zr-tree
-mx1` printed `3 folders, 3 files, 325691 bytes (319 KiB)`,
`Files read from disk: 3`, `Archive size: 90141 bytes (89 KiB)` and
`Everything is Ok`, and exited with code 0. `stderr.txt` was empty. Host Wine's
archive was 90,149 bytes; the stored timestamps compress differently. The run
executed 85.9 million x86 instructions. During compression the interpreter
reported 14.0 million per second, against 4.2 million while decompressing in
console-2/3.

The real application now recursively scans folders, reads files and writes a
new archive with seeks on Switch. Console-6 extracts that archive with
`7zr x C:\wine-nx-tree.7z -oC:\7zr-out -y`. Extraction checks every CRC in
the Switch-made archive. It also needs the file operations that creating an
archive does not:

- Directory creation. The Horizon server's directory path only opened
  existing directories, so `CreateDirectoryW` would fail. For
  `FILE_DIRECTORY_FILE` with a create disposition it now calls `mkdir` first,
  as wineserver's `open_fd` does. An existing directory is accepted unless the
  disposition is `FILE_CREATE`. libnx maps this to `fsFsCreateDirectory`.
- File times. The Switch build has neither `futimes` nor `futimens`, and
  Horizon's file systems offer no call to set timestamps, so `SetFileTime`
  returned `STATUS_NOT_IMPLEMENTED`. It now succeeds without effect, and the
  first call logs
  `[FILE] file times cannot be stored on Horizon; SetFileTime succeeds without effect`.
  Extracted files keep the SD card's own modification times.
- Attributes. libnx `chmod`/`fchmod` return ENOSYS. `SetFileAttributesW`
  called `fchmod` even when the permission bits would not change, as with
  `FILE_ATTRIBUTE_ARCHIVE` or `NORMAL`. It is now skipped in that case, and only
  a real change, setting read-only on a file, reports the failure.
- Directory handles. `NtSetInformationFile(FileBasicInformation)` on a
  descriptor-less Horizon directory now succeeds. Its times cannot be set, and
  `READONLY` is ignored for directories upstream too.

Host Wine 11.0 extracting the host-made archive of the same tree prints
`Everything is Ok`, `Folders: 3`, `Files: 3`, `Size: 325691`, and
`Compressed:` equal to the archive size. It exits 0 and restores the tree byte
for byte. A second run with `-y` prints the same summary. On Switch, expect
`1 file, 90141 bytes (89 KiB)` and `Compressed: 90141`.

The run depends on `C:\wine-nx-tree.7z` from console-5 still being on the SD
card. The package never stages that archive or `C:\7zr-out`, and the verifier
checks both. The build, verifier, host console tests and standalone Horizon
syntax target pass. The new lines add no warnings. Hardware verification is
pending.

## Console-6 hardware result: x86 exception dispatch (console-7)

The console-6 run of `7zr x C:\wine-nx-tree.7z` found no archive: it had been
deleted from the SD card, so extraction was not exercised. The error path
exposed two defects that affect far more than 7-Zip.

- Wrong error. 7-Zip printed `ERROR: No more files.` where host Wine prints
  `ERROR: File not found.` The Horizon server ended every directory scan with
  `STATUS_NO_MORE_FILES`. Wine's `get_cached_dir_data` reports
  `STATUS_NO_SUCH_FILE` for a handle's first query that matches nothing, and
  `FindFirstFileW` maps that to `ERROR_FILE_NOT_FOUND`. The server now tracks
  whether a handle has been queried (`horizon_dir_scan_end_status`).
- Crash in exception dispatch. 7-Zip reports this error by throwing a C++
  exception. The run ended with `[BOX64] status=c00000bb EIP=7bc4e208
  instructions=0` and `exit_code=0xc00000bb`, instead of `System ERROR:` and
  exit code 2. The chain: x86 `RtlRaiseException` → `NtRaiseException`
  (syscall 0xcf) → `wow64_NtRaiseException`. The native `NtRaiseException`
  returns `STATUS_NOT_IMPLEMENTED` on Switch, and WoW64's `raise_exception`
  continues exactly where a native unwind would land. `call_user_exception_dispatcher`
  then correctly redirected the guest to x86 `KiUserExceptionDispatcher`
  (0x7bc4e208). The interpreter refused to resume there. x86 `RtlRaiseException`
  reserves its CONTEXT on the stack without clearing it, and `RtlCaptureContext`
  stores selectors with 16-bit moves (`movw %cs, 0xbc(%eax)`). So `SegCs` was
  `0x????0023`, while `import_context` compared all 32 bits with 0x23. WoW64
  itself compares only `LOWORD(SegSs)`. The context transfer now stores
  selectors as 16-bit values, and the interpreter imports and checks only the
  low 16 bits. A new execution test in the ARM64 container runs a program with
  stale selector bits. It fails with the old comparison, reproducing the
  hardware failure, and passes with the fix.
- EAX after `NtContinue`. The catch continuation resumes through x86 `RtlUnwind`
  → `NtContinue`, whose context carries a meaningful Eax. The syscall gate
  always overwrote Eax with the syscall's status. wow64cpu instead returns
  through the full saved context when `WOW64_CPURESERVED_FLAG_RESET_STATE` is
  set. The bridge host now has an optional `context_replaced` callback;
  winebox64 tests and clears that flag, and the gate then leaves Eax alone.
  `Wow64KiUserCallbackDispatcher` restores `cpu->Flags` after a callback, so
  ordinary syscalls that run callbacks still return their status in Eax.
  Bridge tests cover both cases.

The first-chance native `NtRaiseException` still returns
`STATUS_NOT_IMPLEMENTED` on Switch, because native exception dispatch is a
stub. For x86 exceptions that is equivalent: `raise_exception` resumes at the
continuation a native unwind would reach. A second-chance raise terminates the
process as upstream does.

Workload: `7zr x C:\no-such-archive.7z -oC:\7zr-out -y`, 7-Zip's error path.
Under host Wine 11.0 it prints `ERROR: File not found.`, the path,
`System ERROR:` and `File not found.`, then exits with code 2. The `System ERROR:`
lines come from the catch handler, so they appear only if the throw was
dispatched, the stack unwound and the catch continuation resumed. The package
also stages `samples/7zr-x86/7zr-tree.7z`, the tree archived by 7zr under host
Wine (90,149 bytes, SHA-256 recorded), so extraction no longer depends on SD
card state: `7zr x C:\7zr-tree.7z -oC:\7zr-out -y`.

Host verification: the Box64 execution tests in the ARM64 container, the bridge
tests, the console, thread and wait host tests, the standalone Horizon syntax
target, the package build and the verifier all pass. The new lines add no
warnings. Hardware verification is pending.

## Console-7 hardware result: exceptions pass (and extraction, console-8)

`nx-wow64-console-7` passed on hardware. `7zr x C:\no-such-archive.7z` printed
`ERROR: File not found.`, the path, `System ERROR:` and `File not found.`,
matching host Wine 11.0 line for line, and exited with code 2 through its normal
return from `main`. The syscall trace shows the x86 C++ exception path end to
end:

1. `NtSetInformationThread` stores the raise context.
2. `NtRaiseException` returns `NOT_IMPLEMENTED` natively, and WoW64 continues.
3. `NtQueryInformationThread`/`NtSetInformationThread` redirect the guest to
   `KiUserExceptionDispatcher`.
4. The x86 handlers run in the interpreter.
5. One more context set, from `NtContinue` after the unwind.
6. The catch handler writes `System ERROR:`.

Real applications can now throw and catch C++ exceptions and use
`RaiseException`.

Extraction is next. To find what it needs beyond the earlier fixes, 7zr's
`x` of the staged archive was traced under host Wine with a warm, `+server`
traced wineserver, keeping only that run's requests. The one request it uses
that the Horizon server lacked was `set_fd_eof_info`. 7-Zip pre-sizes larger
output files right after creating them: `eof=0x3f7a0` for the 260,000-byte text
and `0x10000` for the binary. The server now implements it
(request 277, layout checked against `server_protocol.h`) with `ftruncate`.
libnx maps that to `fsFileSetSize`, which grows as well as shrinks.
Directories give `STATUS_FILE_IS_A_DIRECTORY`. As upstream, no access check
is made beyond the descriptor's own mode.

`set_fd_name_info` (rename, e.g. `MoveFileExW` and 7-Zip updating an existing
archive) and `set_fd_disp_info` (delete through `SetFileInformationByHandle`)
are still missing. Horizon cannot rename or delete an open file, so both need
their own design.

Workload: `7zr x C:\7zr-tree.7z -oC:\7zr-out -y` on the staged archive. Host
Wine prints `1 file, 90149 bytes (89 KiB)`, `Everything is Ok`, `Folders: 3`,
`Files: 3`, `Size: 325691` and `Compressed: 90149`, exits 0, and restores the
tree byte for byte. The package build, verifier, standalone Horizon syntax
target and host tests pass. The new lines add no warnings. Hardware
verification is pending.

## Console-8 hardware result: extraction passes (and in-place rewrite, console-9)

`nx-wow64-console-8` passed on hardware. `7zr x C:\7zr-tree.7z -oC:\7zr-out -y`
printed `Everything is Ok`, `Folders: 3`, `Files: 3`, `Size: 325691` and
`Compressed: 90149`, matching host Wine line for line, and exited 0 with an
empty stderr. Every CRC matched. The syscall trace for the extraction contains
no unexpected failure, only the probes Windows answers the same way:

- `NtQueryAttributesFile` "not found" twice, and `NtQueryDirectoryFile`
  `STATUS_NO_SUCH_FILE` 12 times: existence checks before creating.
- `NtCreateFile` `STATUS_OBJECT_NAME_COLLISION` 9 times: 7-Zip calls
  `CreateDirectoryW` on every path component for each file.

All 19 `NtSetInformationFile` calls succeeded: end of file, times and
attributes. The server created `7zr-out\7zr-tree\data\text` and reopened
each folder with backup semantics to set its times and attributes. The one-time
`[FILE] file times cannot be stored` line appeared. With the error path
(console-7), creation (console-5) and testing (console-2/3), 7-Zip's main
workflows now run on Switch.

Updating an archive in place needs more. A host trace of
`7zr rn rn.7z 7zr-tree\readme.txt 7zr-tree\README-renamed.txt`, diffed against
the Horizon server's handled requests, differs from the passing `x` run only in
`set_fd_name_info`. 7-Zip writes `rn.7z.tmp`, deletes `rn.7z` with
delete-on-close, opens the temporary file with `DELETE | SYNCHRONIZE` and
renames it into place (`flags=0`). The server now implements:

- `set_fd_name_info` (request 276). The client resolves the target's unix name.
  A name relative to a root directory handle is joined to that directory's
  name. The checks follow wineserver's `set_fd_name`
  (`horizon_rename_check`): the same file is left alone, and an existing file
  collides without `FILE_RENAME_REPLACE_IF_EXISTS`. A directory or an open file
  cannot be replaced; `stat` failing with EIO means the target is open for
  writing. Horizon's file system cannot rename an open file, so the object's own
  descriptor is closed around the rename and reopened under the new name, with
  the same access and position. Another handle open on the source gives
  `STATUS_SHARING_VIOLATION`, as does FS result 0xe02 from `rename`. Hard links
  (`link`) are `STATUS_NOT_SUPPORTED`, because FAT has none. A case-only rename
  counts as the same file and changes nothing, as in upstream.
- `set_fd_disp_info` (request 275), i.e. `SetFileInformationByHandle` with
  `FileDispositionInfo`, which MSVC's `std::filesystem::remove` uses. It
  follows `set_fd_disposition` (`horizon_disposition_update`): `DELETE` marks
  the object for deletion at its last close, and `ON_CLOSE` withdraws a
  delete-on-close given at open. It requires `DELETE` access and refuses a
  non-empty directory with `STATUS_DIRECTORY_NOT_EMPTY`. The close path deletes
  when either the open option or the disposition asks.

Both layouts were checked against `server_protocol.h`, and both decisions have
host tests against Wine's constants. Workload: `7zr rn` on a staged copy of the
tree archive, `C:\7zr-rename.7z`. Host Wine prints
`Keep old data in archive: 3 folders, 3 files, 325691 bytes (319 KiB)`,
`Add new data to archive: 0 files, 0 bytes`, `Archive size: 90160 bytes
(89 KiB)` and `Everything is Ok`, and exits 0; a rerun prints the same. The
package build, verifier, standalone Horizon syntax target and host tests pass.
The new lines add no warnings. Hardware verification is pending.

## Console-9 hardware result: in-place update passes (next: benchmark threads)

`nx-wow64-console-9` passed on hardware. `7zr rn C:\7zr-rename.7z
7zr-tree\readme.txt 7zr-tree\README-renamed.txt` printed
`Keep old data in archive: 3 folders, 3 files, 325691 bytes (319 KiB)`,
`Archive size: 90160 bytes (89 KiB)` and `Everything is Ok`, matching host Wine,
and exited 0. The Horizon trace shows the original deleted on close and then
`[HZFILE] rename handle=00000244 target=sdmc:/switch/wine/drive_c/7zr-rename.7z
flags=0 action=0 status=00000000`: the temporary archive was renamed into place.
Every 7-Zip workflow tried so far now passes on Switch: test, create from a
folder tree, extract, update in place, and the C++ exception error path.

The next workload uses a real application's worker threads:
`7zr b 1 -mmt2 -md18`, one LZMA pass with two threads. Under host Wine it runs
for 12 seconds. Its server traffic is thread- and synchronization-heavy:
3,823 `select`, 3,526 `release_semaphore`, 172 `create_semaphore`, 260
`event_op`, and thread creation and termination. It also makes 767
`get_process_info` calls from `GetProcessTimes`. Diffed against the handled
requests, its only gap is `get_process_info`, which the passing runs also
sent. On Switch it fails, so `GetProcessTimes` fails and 7-Zip uses
`GetTickCount` for its usage column. That fallback is deliberate: reporting
zero CPU time would feed a division in the rating. No code changed for this
run. The benchmark's first thread takes the `[LIFECYCLE]` baseline, so the
final verdict also checks that the benchmark's thread resources were reclaimed.
Success: `[STDOUT]` rows up to `Tot:`, the lifecycle verdict, and
`exit_code=0x00000000`. The speed figures are device-dependent.

## Benchmark hardware checkpoint: processor information (console-10)

The first benchmark run (console-9 binary) printed 7-Zip's header and system
line, then was closed about 20 seconds in, during 7-Zip's CPU-frequency
measurement. The interpreter was still computing at 33 million instructions per
second, the fastest yet for a tight loop. No benchmark rows were reached. The
system line exposed a compatibility bug that affects any program sizing its
threads from `GetSystemInfo`:

```text
host Wine:  x86 6.2C00 threads:8 4GB - x64 6.2C00 threads:8 4GB
Switch:     ARM 2336 0.0 act:7 threads:0 4GB - x64 2336 0.0 act:7 threads:0 4GB f:54C
```

The x86 program was told the processor is ARM, level 2336, with 0 processors
(`act:7` is the 3-core application mask). The ` - x64` half is upstream's
intended `GetNativeSystemInfo` answer for WoW64 on a non-AMD64 host. There were
two causes:

- `init_cpu_info()` was never called. Upstream calls it from
  `start_main_thread`, which the Switch runtime replaces, so
  `peb->NumberOfProcessors` stayed 0 and `wine_nx_init_wow64_peb` copied that
  0 into the 32-bit PEB. `SYSTEM_CPU_INFORMATION.MaximumProcessors` becomes
  `dwNumberOfProcessors`. The runtime now calls `init_cpu_info()` right after
  the first TEB, logs `[INIT] processors=N`, and does so before the WoW64 PEB is
  built. Its ARM64 CPU-model parsing reads `/proc/cpuinfo`, so on Switch it now
  supplies the Tegra X1's Cortex-A57 (part 0xd07, r1p1) instead of zeros.
- winebox64 did not export `BTCpuUpdateProcessorInformation`. For
  `SystemCpuInformation`, WoW64 asks the host for
  `SystemEmulationProcessorInformation`, which on ARM64 describes ARM. It then
  lets the CPU DLL describe the emulated processor, as FEX does. winebox64 now
  does: `PROCESSOR_ARCHITECTURE_INTEL`, level 15, revision 0x0209, and the
  feature bits Wine's i386 `get_cpu_features` derives from the same `PF_*`
  answers. The host's processor count is kept.

The CPU identity now lives in one header, `dlls/winebox64/cpuid.h`: the CPUID
signature and EDX features, `IsProcessorFeaturePresent` answers, and the x86
`SYSTEM_CPU_INFORMATION`. The interpreter's CPUID and winebox64 both use it.
`tests/winebox64_cpuid.c`, run by `check-wow64-box64-bridge.sh`, checks that
the three agree bit by bit and that nothing beyond SSE2 is claimed. The package
verifier requires the new export. The interpreter's container tests, including
CPUID and RDTSC, the standalone Horizon syntax target and the thread tests
pass.

Next run (build `nx-wow64-console-10`): the same benchmark. Expect a system line
starting `x86` with `threads:3`. 7-Zip's CPU-frequency measurement alone runs
tens of millions of instructions per sample under the interpreter, so let it
run for several minutes, until the `Tot:` row and `[EXIT]`.
Hardware verification is pending.
