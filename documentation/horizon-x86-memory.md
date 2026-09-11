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

The actual NFS executable and its DLL closure have not been supplied or found
among the inspected samples. Its image base and relocation requirements remain
unknown. This prevents claiming a game-specific launch profile, but does not
prevent implementing or testing the memory architecture.

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
