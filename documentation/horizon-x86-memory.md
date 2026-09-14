# Horizon x86 memory architecture — source audit

Date: 2026-09-10. This supersedes the address-space gate and integration
assumptions in the supplied `nfsu-horizon-plan.html`. It does not claim that a
32-bit game, the new runtime change, or a new launch profile has run on hardware.

## Decision

Keep guest addresses identical to host virtual addresses for the first x86
bring-up. Use **36-bit VA for a relocatable PE32 console milestone**, with guest
allocations constrained below 2 GiB where Wine requests that limit. Evaluate
**ARM64 execution with 32-bit VA for fixed low-base executables**. Do not start a
base-plus-offset dynarec rewrite merely because the host heap is above 4 GiB.

Use **wine-nx as the implementation tree**, retaining its native ARM64 Wine
runtime. Integrate Box64 through a WoW64 CPU backend; the sibling winebox64_nx
is reference material for the execution core and libnx integration. Its x64
Linux syscall/ELF layer is not the boundary used by this native Wine design.
See [the CPU interface contract and implemented bridge](wow64-box64-interface.md).

The actual NFS executable was supplied on 2026-09-12 and measured from its PE
headers: `SPEED2.EXE`, machine `0x014c`, `ImageBase 0x00400000`,
`SizeOfImage 0x00532000` (extent `0x00400000 .. 0x00932000`),
`IMAGE_FILE_RELOCS_STRIPPED` set and an empty base-relocation directory
(`rva 0, size 0`). It therefore requires its preferred address; it is not
relocatable to another base.

A hardware run on `nx-wow64-dynarec-54` under the default 39-bit profile failed
in the loader, before any Direct3D call: the trace reports
`[VA] Horizon ASLR region base=0x8000000`, the image was placed at `0x08000000`,
and `perform_relocations` refused it for lack of relocation records, giving
`STATUS_CONFLICTING_ADDRESSES`. This is the fixed-low-base case step 4 below
reserves for the constrained 32-bit-VA experiment, now with measured operands
rather than assumed ones. For contrast, `quake3e.exe` and `openttd.exe` share
`ImageBase 0x00400000` but retain relocation directories, and both load after
being relocated into the 39-bit envelope.

## What the kernel and launcher establish

The following are virtual address envelopes, **not free RAM or reservations**:

| Launch VA profile | Alias-code envelope, end exclusive | Relevant layout property |
| --- | --- | --- |
| `39_bit` | `0x08000000 .. 0x8000000000` | Heap/alias placement can exclude low ranges; 128 MiB null guard |
| `36_bit` | `0x08000000 .. 0x1000000000` | Heap/alias live in the large region starting at 2 GiB; 128 MiB null guard |
| `32_bit` | `0x00200000 .. 0x100000000` | Heap/alias live above 1 GiB; heap region is 1 GiB |
| `32_bit_without_alias` | `0x00200000 .. 0x100000000` | Heap grows to 2 GiB, alias region is absent; still only 4 GiB VA total |

These follow from [the address constants][memory-map], [address-space mode
selection][address-info], and [page-table initialization][page-table]. The
36-bit layout protects the region below 2 GiB from heap/alias reservations, but
it does not protect it from loader code, NROs, stacks, TLS, or other mappings.
The 32-bit layout likewise does not promise that a particular low image base or
`0x7ffe0000` is free. On that profile the latter can collide with heap/alias
regions. A runtime allocator must check these exclusions and actual occupancy.

[Atmosphère's HBL config parser][override] accepts all four modes. Its defaults
select 39-bit. The [metadata loader][meta] changes the address-space bits without
clearing the ARM64 instruction flag; [CreateProcess validation][create-process]
allows that combination. Thus `32_bit` does not mean executing ARM32 code.
The upstream [hbloader metadata][hbl-meta] itself specifies ARM64 plus the old
36-bit address mode, but Atmosphère's effective override takes precedence.

For a dedicated title override, use the existing `program_id_N` and
`override_key_N` entry with `override_address_space_N=36_bit` (or `32_bit` for
the constrained fixed-base experiment). Do not silently change the global
`override_any_app_address_space` preference. No SD configuration was changed
during this audit.

[GetInfo IDs 12/13][get-info] return the alias-code region's bounds. The runtime
may query these at startup to select behavior; this is ordinary configuration
discovery, not a standalone exploratory NRO or an architectural gate.

## Constrained 32-bit-VA experiment

`SPEED2.EXE` needs `0x00400000 .. 0x00932000`, which only the `32_bit` and
`32_bit_without_alias` envelopes can contain. The launch profile is a property
of process creation, not of the NRO: an already-running process cannot change
its layout, but Atmosphère selects one before the process starts. Add a
dedicated title override rather than changing the global
`override_any_app_address_space` preference:

```ini
[hbl_config]
program_id_1=<title id used to launch wine-nx>
override_key_1=<the button already used for that entry>
override_address_space_1=32_bit
```

What the runtime already does: `horizon_get_address_space_limits`
(`dlls/ntdll/unix/horizon.c`) reads GetInfo 12/13 and feeds
`address_space_start` and `host_addr_space_limit` in
`dlls/ntdll/unix/virtual.c`, so the allocator follows whatever envelope the
kernel reports. `0x08000000` appears only as the fallback when the query fails.
No code asserts the 128 MiB floor.

What the profile change puts at risk, in the order a hardware log would show it:

1. **`0x7ffe0000` (`user_shared_data`, `dlls/ntdll/unix/virtual.c`).** A Windows
   ABI constant at about 2 GiB, and a fixed requirement rather than a preference.
   Under `32_bit` the heap and alias regions live above 1 GiB and may claim it.
   The 39-bit trace shows it mapping cleanly (`[VA] base=0x7ffe0000 ... out=0x7ffe0000/0x1000`);
   that result does not carry over to a different envelope. See
   [KUSER_SHARED_DATA away from its address](#kuser_shared_data-away-from-its-address).
2. **Native module placement.** The 39-bit trace shows the ARM64 modules trying
   their preferred `0x180000000` (6 GiB), failing with `rc=0xd401`, and falling
   back. That address is outside a 4 GiB envelope entirely, so every native
   module depends on the fallback path succeeding below `0x100000000`.
3. **Capacity.** `32_bit` gives a 1 GiB heap region (2 GiB for
   `32_bit_without_alias`) against the `memory=3189 MB` the 39-bit run reports.
   Wine, the Box64 code cache, Mesa and the guest allocations must all fit.
   The limit is on memory, not only address space: mesosphere sets a process's
   `m_max_process_memory` to its heap region size, so the 32-bit run reports
   `memory=1024 MB`, and `32_bit_without_alias` folds the alias region into the
   heap for 2 GiB. On `nx-wow64-dynarec-60`, once DirectSound worked, NFSU2 ran
   out within seconds of starting: wined3d failed `wined3d_resource_allocate_sysmem`
   and the game read a null pointer. `[PROGRESS]` reports `heap_used_mb` and
   `heap_free_mb` from `nx-wow64-dynarec-61` on.
4. **The executable range itself.** `0x00400000 .. 0x00932000` must be free of
   loader code, NROs, stacks and TLS; the envelope permits the range but does not
   reserve it.

Failure at 3 or 4 is what step 4 below calls "capacity/collision failure", and
is the point at which a deliberate launcher/kernel or translation design becomes
the remaining option. Nothing here predicts that the game runs once it loads.

### KUSER_SHARED_DATA away from its address

Under `32_bit` the kernel places the 1 GiB heap and alias regions at random in
the upper 3 GiB on every launch, and `MapProcessCodeMemory` refuses both, so
whether `0x7ffe0000` can be mapped is luck. On `nx-wow64-dynarec-57` (hardware,
2026-09-12) the alias region was `0x67c00000 .. 0xa7c00000` (the launch before,
`0x5e600000 .. 0x9e600000`): the allocation at `0x7ffe0000` failed with
`STATUS_CONFLICTING_ADDRESSES` and `virtual_alloc_first_teb` moved the page to
`0x200000`. SPEED2.EXE's imports then loaded, and the process died on
`[EXC] far=0x7ffe0320 kind=read` - TickCount - with `lr` in Box64's
`arm64_next`: translated x86 code, reading the Windows address. Moving the page
updated only the unix-side pointer. i386 kernelbase (GetTickCount,
GetTickCount64, QueryInterruptTime) and kernel32 (GetTickCount,
GetTickCount64, GetSystemDEPPolicy) fold `0x7ffe0000` into their code, and
`dlls/win32u/message.c`, built into the runtime, does the same natively.

Every Wine consumer now reaches the real page:

- ntdll exports its `user_shared_data` variable as the private
  `wine_nx_user_shared_data`. The runtime sets it in the ARM64 image
  (`wine_nx_patch_ntdll_dispatchers`, `dlls/ntdll/loader.c`) and in the i386
  image (`wine_nx_prepare_wow64_ntdll`, `dlls/ntdll/unix/loader.c`).
- kernelbase and kernel32 look that export up when they attach, keeping
  `0x7ffe0000` when ntdll lacks it, as on ordinary Wine.
- The runtime's win32u uses ntdll's unix-side pointer.

A program can still read `0x7ffe0000` itself. That read faults, and
`__libnx_exception_handler` (`dlls/ntdll/unix/horizon.c`) emulates the AArch64
load - single, pair, SIMD, any addressing mode, writeback included - from the
real page and continues (`horizon_read_redirect.h`, tested against the
processor in `wine-nx-probe/tests/horizon_read_redirect.c`). Writes stay access
violations, as on Windows, where the page is read-only. Each such read costs an
exception, and libnx's single exception stack makes simultaneous faults on two
threads unsafe, so `[USD]` log lines report the count at every power of two:
many of them mean a program polls the page and needs a better answer.

On `nx-wow64-dynarec-58` (hardware) the alias region again covered the page
(`0x40c00000 .. 0x80c00000`), the page moved, and nothing read the old address:
no `[USD]` line, no fault there. SPEED2.EXE went on to start its own thread and
winmm's timer thread, then the main thread wrote to `0x2c` from translated code
right after `NtOpenDirectoryObject` returned `STATUS_NOT_IMPLEMENTED` - the
Horizon server had no `open_directory`. Its i386 callers are
`BaseGetNamedObjectDirectory` (followed by a create syscall that never came),
`GetLogicalDrives` and `QueryDosDeviceW(NULL)`, which return an empty drive list
without another syscall, and user32's window-station directory. Build 56's
crash, a write to `0x2c` after the same two threads, fits the same point.

`nx-wow64-dynarec-59` gives the server `\??` (also `\DosDevices`, `\GLOBAL??`),
listing `C:`, and `\BaseNamedObjects` / `\Sessions\<n>\BaseNamedObjects`, empty
(`dlls/ntdll/unix/horizon_object_dirs.h`, `wine-nx-probe/tests/horizon_object_dirs.c`).
Other directories, `\KnownDlls` and the window stations included, answer
`STATUS_OBJECT_NAME_NOT_FOUND`. A named object created relative to a
BaseNamedObjects handle keeps its bare name with root 0, so objects match as
they did while the directory could not be opened. The fatal-fault path now logs
every register and, for a pc in translated code, the x86 instruction address
and guest registers (`[BOX64 FAULT]`), and `[SYSCALL]` lines carry the thread id.

On `nx-wow64-dynarec-59` (hardware) SPEED2.EXE reached its title screen, "Press
Enter Key", drawing about 58 frames a second through OpenGL. What remained was
not about memory. DirectSound logged `DSOUND_ReopenDevice Initialize failed:
80070057`: winenxaudio.drv reported a plain PCM mix format, DirectSound makes
the mix format 32-bit float EXTENSIBLE without touching cbSize, and mmdevapi
rejects EXTENSIBLE with a cbSize below 22. And the Horizon server answered every
keyboard `send_hardware_message` with `STATUS_NOT_IMPLEMENTED`, so no controller
key reached any program; DirectInput 8 reads the keyboard only through raw input
(`WM_INPUT`), which the server lacked as well. `nx-wow64-dynarec-60` reports an
EXTENSIBLE mix format and decodes float inside EXTENSIBLE
(`wine-nx-probe/source/audio_unix.c`), queues keyboard input as server/queue.c
does (`dlls/ntdll/unix/horizon_keyboard.h`), keeps raw input registrations and
delivers `WM_INPUT` with its RAWKEYBOARD, and sends scan codes with the extended
flag so DirectInput can tell the arrows apart. A program's own `NAME.keys.txt`
remaps the controller for it, A and B included; NFSU2's makes A Enter and B
Escape.

With the forwarder on `32_bit_without_alias`, `nx-wow64-dynarec-61` reported
`memory=2048 MB` (heap region `0x4e400000 .. 0xce400000`) and still froze a few
seconds in. Direct3D's command-stream thread failed to commit 0x90000 bytes
(`STATUS_ACCESS_DENIED`): Wine had reserved `0x4ae0000 .. 0x4b70000` over a libnx
thread stack mirror at `0x4af2000 .. 0x4bf2000`, and `svcMapProcessCodeMemory`
refused the commit with `0xd401`, InvalidCurrentMemory - the destination was not
free. wined3d kept a buffer without memory, Mesa's nouveau driver copied from its
null pointer, and the fault parked that thread, which the game then waited on.
A 32-bit process's stack region is the small map itself, and so is the code
region libnx takes JIT memory from; Wine's free-area search sees neither.
`nx-wow64-dynarec-62` checks each reservation with svcQueryMemory
(`dlls/ntdll/unix/horizon_free_range.h`) and answers `EEXIST` where the kernel
has the range mapped, so Wine moves on to the next free area.

`nx-wow64-dynarec-62` still froze, with 1.5 GiB of heap free: a write fault in
translated code was handled and resumed, and the next instruction faulted with
EDI holding the address of the first. The libnx dump is restored in user mode,
and the final branch needs a register; `horizon_restore_exception_context` gives
up x17, which Box64 uses for the guest's EDI (x16 is ESI). tico-dolphin resumes
its JIT the same way and keeps x17 out of its register pool for that reason.
`nx-wow64-dynarec-63` resumes translated code through x9, which Box64 never uses
(`wine-nx-probe/tests/check_exception_restore.py` runs both restores). A
restore that clobbers nothing would have to finish before libnx's
`svcReturnFromException`, by replacing its weak `__libnx_exception_entry`.

## Mapping and protection contract

`MapProcessCodeMemory` requires a valid process handle, aligned source/destination
and size, suitable source pages, an eligible destination region, and a free
destination. `CanContain()` excludes heap and alias regions even when QueryMemory
reports unmapped pages there. The mapping locks the source and removes its user
access. Retaining a backing allocation does not mean it remains writable through
its original pointer. See [MapCodeMemory and CanContain][page-table].

`virtmemAddReservation()` only inserts a node in libnx's allocator bookkeeping.
It does not check that a fixed range is free and does not reserve kernel page
tables. See [libnx virtmem][virtmem]. Mapping success is authoritative; ownership,
locking, rollback and coordination with all native allocators remain necessary.

Use separate policies for translated guest pages and native generated code:

1. **Guest x86 pages:** keep direct host pointers. Back them with page-aligned
   heap allocations mapped into the guest VA. Track Windows execute permission
   as emulator metadata; x86 bytes do not need ARM execute permission. After
   the initial AliasCode-to-RW transition, the kernel state is AliasCodeData.
   Use `svcSetMemoryPermission` for subsequent R/RW/None data transitions.
   Implement PAGE_GUARD and dirty-code invalidation through fault delivery and
   emulator metadata; the syscall alone does not implement Windows semantics.
2. **ARM64 dynarec cache:** keep libnx JIT RW/RX aliases, cache maintenance, and
   generated-code ownership separate from guest data. Do not translate guest
   PROT_EXEC directly into making x86 backing pages ARM RX.
3. **Native ARM64 Wine code:** requires real execute transitions and therefore
   has a different lifecycle from translated guest pages.

There is a concrete trap in the current `dlls/ntdll/unix/horizon.c` implementation:
`set_code_memory_perm()` always calls `svcSetProcessMemoryPermission`, including
after RW. The kernel's [memory-state flags][memory-state] remove `FlagCode` when
AliasCode becomes AliasCodeData; SetProcessMemoryPermission requires FlagCode.
SetMemoryPermission instead requires CanReprotect, present on AliasCodeData.
Consequently the existing mapping layer is useful reference code, but cannot be
copied as proof of arbitrary repeatable VirtualProtect transitions. Native
RW-to-RX restoration needs a deliberate alias/remap strategy with threads stopped
and backing bytes preserved. The document's two-pass static-loader sequence
does not by itself solve dynamic Wine protection changes.

Required syscall inventory must match the selected implementation:

- Guest AliasCode data lifecycle: `0x02`, `0x73`, `0x77`, `0x78`, own-process handle.
- Current Box64 JIT implementation: `0x4b`, `0x4c` (it explicitly rejects libnx's
  SetProcessMemoryPermission JIT fallback).
- `0x74`/`0x75` are needed if using MapProcessMemory views; they are not a
  universal prerequisite for the above design.

[hbloader][hbl-main] provides the process handle and syscall hints;
[libnx env][env] reads those ABI entries. Hints describe the launch contract,
not an executed mapping test. [libnx JIT][jit] also shows why a blanket statement
that applet mode necessarily disables JIT cannot be inferred from its mode name.
Continue to use title override for the project's memory budget.

## Wine and Box64 corrections

- Box64's overlaid `src/emu/x64run.c:Run` selects `is32bits` using `CS == 0x23`
  without an enclosing BOX32 conditional. The [pinned ARM64 decoder][box-decoder]
  also contains 32-bit paths. Absence of `-DBOX32` is not proof that decoding is
  absent. Test execution, address-size/segment semantics, and far transitions
  before introducing BOX32 loader/wrapper dependencies.
- The existing runtime only accepts x64 ELF programs and starts them with
  `CS=0x33`. That is consistent with new WoW64's x64 Unix side; PE32 is a Windows
  module loaded later. Native i386 ELF/syscalls are not the initial target.
- `dlls/ntdll/unix/loader.c:reexec_loader` and `check_command_line` in this Wine
  tree still have exec paths. Renaming/removing the wine64 executable does not
  eliminate bootstrap re-exec. Trace the actual built loader/preloader contract
  and implement the needed in-process handoff. The historical PE32 failure log
  alone does not establish which architecture transition failed.
- `virtual.c` uses fixed shared user data at `0x7ffe0000` and WoW64 allocation
  limits. `virtual_relocate_module` rejects rebasing when RELOCS_STRIPPED is set.
  A PE at `0x00400000` therefore cannot be identity-mapped in the 36/39-bit
  envelopes unless it can be relocated. DYNAMIC_BASE being absent does not by
  itself mean relocation records are absent.
- In `winebox64_nx`, arbitrary `runtime_mmap` allocations use host calloc/shared
  backing pointers, MAP_32BIT has no dedicated implementation, and mprotect is
  a successful validity check without a protection change. These must change
  before treating a console success as an adequate game memory implementation.
- A base-plus-offset fallback would affect the interpreter, instruction fetch,
  dynarec loads/stores and atomics, mixed 32/64-bit WoW64 transitions, syscall
  buffers and nested pointers, and native library callbacks. It is not merely
  an extra add instruction in the dynarec memory path.

## Concrete changes made and next implementation sequence

The sibling `winebox64_nx` now queries the configured ASLR bounds when exact
mapping is enabled, replacing its hard-coded 128 MiB rejection floor. The filter
checks the entire range without overflow; if GetInfo fails it defers to the
kernel rather than inventing a layout. This permits attempting a valid 4 MiB
mapping under 32-bit VA. It does **not** supply a low allocator or change the
existing kernel mapping/protection mechanism.

`winebox64_nx/scripts/inspect-pe-addresses.py` reads EXE/DLL headers offline and
reports architecture, image extent, large-address-awareness, stripped relocation
flags and relocation-directory metadata against each address envelope. It does
not execute binaries or claim to validate relocation records/import closure.
From the sibling repository:

```sh
python3 scripts/inspect-pe-addresses.py /path/to/game.exe /path/to/game.dll
```

Proceed in this order:

1. Attach the pinned Box64 execution core to the native WoW64 bridge in wine-nx.
   Build a relocatable i386 payload with a preferred base such as `0x10000000`;
   exercise 32-bit execution and a round trip through the registered native
   call gate. Build ARM64 Wine/WoW64 modules and the i386 PE module closure.
2. Add a bounded allocator honoring fixed requests, NOREPLACE, alignment,
   MAP_32BIT's below-2-GiB requirement when applicable, and Wine's WoW64 limits.
   Coordinate with libnx reservations and kernel exclusions; never return an
   arbitrary high pointer for a low-allocation request. Reserve fixed Windows
   ranges before competing runtime/JIT allocations where launch order permits.
3. Implement the data-page lifecycle above, partial unmap/protect, ownership,
   fault recovery and code-cache invalidation. Keep the existing PE64 path as
   a reference regression target in the sibling; retain native ARM64 Notepad
   as the regression target in wine-nx. Do not fake protection success.
4. Extend this runtime's native bootstrap to enter WoW64 and run a positive PE32 console
   assertion. Then inspect the real game's EXE/DLL requirements. A fixed low
   image gets the constrained 32-bit-VA experiment; capacity/collision failure
   triggers a deliberate launcher/kernel or comprehensive translation design.
5. Validate the allocator, protection/fault transitions and sustained execution
   in the runtime on hardware. This validates implementation, not an unknown
   address-space constant. Graphics follows a genuinely working PE32 path.

Initial audit validation: host C boundary tests with AddressSanitizer/UBSan; seven
Python PE fixture tests; inspection of the existing ARM64 GUI smoke executable;
git whitespace checks. That initial audit did not build an NRO. Subsequently,
Docker was started, the pinned execution core was fetched into wine-nx, and real
x86 gate-round-trip tests passed in ARM64 Linux. The same test cross-compiles
and links as a Switch NRO; see [current integration status](wow64-box64-interface.md).
The full Wine package and a hardware run remain unverified.

## Source revisions

Local reference snapshots inspected:

- Atmosphère: `cb4b882e3b176480ac57a1161a85ff175c3f162c`
- libnx: `dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb`
- wine-nx, before this document: `eaa5b16e71f4c479ac84f308747d4d37a17dc36d`
- winebox64_nx, before these changes: `fd6e751a41315d00950c80dab193d8fee8718b4e`
- Box64 upstream: `dae0917c47b4edd8956f314210417a20fd225c4b` plus the local overlay
- nx-hbloader upstream master inspected: `82b95122c5ae8dc059bf23893ba7623c72c86773`

[memory-map]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/libraries/libvapours/include/vapours/svc/svc_memory_map.hpp
[address-info]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/libraries/libmesosphere/source/kern_k_address_space_info.cpp
[page-table]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/libraries/libmesosphere/source/kern_k_page_table_base.cpp
[override]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/libraries/libstratosphere/source/cfg/cfg_override.board.nintendo_nx.inc
[meta]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/stratosphere/loader/source/ldr_meta.cpp
[create-process]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/libraries/libmesosphere/source/svc/kern_svc_process.cpp
[get-info]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/libraries/libmesosphere/source/svc/kern_svc_info.cpp
[memory-state]: https://github.com/Atmosphere-NX/Atmosphere/blob/cb4b882e3b176480ac57a1161a85ff175c3f162c/libraries/libmesosphere/include/mesosphere/kern_k_memory_block.hpp
[virtmem]: https://github.com/switchbrew/libnx/blob/dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb/nx/source/kernel/virtmem.c
[env]: https://github.com/switchbrew/libnx/blob/dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb/nx/source/runtime/env.c
[jit]: https://github.com/switchbrew/libnx/blob/dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb/nx/source/kernel/jit.c
[hbl-meta]: https://github.com/switchbrew/nx-hbloader/blob/82b95122c5ae8dc059bf23893ba7623c72c86773/hbl.json
[hbl-main]: https://github.com/switchbrew/nx-hbloader/blob/82b95122c5ae8dc059bf23893ba7623c72c86773/source/main.c
[box-decoder]: https://github.com/ptitSeb/box64/blob/dae0917c47b4edd8956f314210417a20fd225c4b/src/dynarec/arm64/dynarec_arm64_00.c
