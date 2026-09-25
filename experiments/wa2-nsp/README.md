# WA2 NSP experiment in Autorun

This directory publishes the source and repeatable **method** used to build a
White Album 2 homebrew NSP. It belongs to the existing
[YangStark/autorun](https://github.com/YangStark/autorun) project. The branch is
based on `integration/9b71705-all` at
`25623410b163fbdde540594a098a898ca226c2a2`; the Wine changes live in the
normal source tree, and the NSP loader, bundle builder, packager, and host
checks live here. The earlier
[`feature/game-home-forwarder`](https://github.com/YangStark/autorun/tree/feature/game-home-forwarder)
work is the related forwarder experiment, not a prerequisite checkout for
this branch.

## What can be reused

1. `include/wine/nx_root.h` and the Wine/launcher changes give this game a
   dedicated SD root, so registry, configuration, saves, logs, and runtime
   files resolve under `/switch/autorun-games/wa2-full`.
2. `work/wa2-complete-nsp/loader` deploys the smaller runtime bundle on first
   launch, verifies it, and uses a ready marker for later launches.
3. `work/wa2-complete-nsp/tools/make_bundle.py` streams a PAK-free bundle.
   `packaging/make_nsp.py` streams the large PAKs directly into Program RomFS;
   the Wine path mapping reads these immutable resources from the installed
   title instead of copying a second 7 GiB tree to SD.
4. `work/generate_asset_header.py` turns a local asset manifest into the
   compile-time mapping. `work/validate-wa2-presentable.py` checks a finished
   package's layout and hashes. The included host tests cover deployment,
   bundle bounds, streaming layout, and cache failure/retry paths.

These are **WA2-specific source examples**, not a turnkey converter for any
Windows game. Paths, title ID, executable, file formats, save behavior, and
runtime compatibility require fresh investigation for another game.

## Local inputs and build order

The repository intentionally excludes all game PAKs and EXEs, the original
snowflake icon, local keys, saves, built runtime binaries, generated package
headers, and the NSP. Obtain and prepare your own authorized inputs locally.
The core local manifest is an ordered JSON array of objects with exactly
`path`, `pkg`, `size`, and `sha256`. The packager reads the same array as the
header generator; its order determines the RomFS package table. A single
redacted *shape* example is:

```json
[{"path":"example.pak","pkg":"wa2-asset-001.pak","size":123,"sha256":"<64 lowercase hex digits>"}]
```

For the checked-in WA2 code, use `wa2-full-assets.json` locally at
`experiments/wa2-nsp/work/`. Generate the private header before building the
Wine runtime:

```sh
python3 experiments/wa2-nsp/work/generate_asset_header.py \
  --manifest experiments/wa2-nsp/work/wa2-full-assets.json \
  --output include/wine/nx_package_assets.h
```

The generator creates an ignored file. Keep the matching input PAKs under a
local asset root and a PAK-free runtime payload separately. The runtime build
needs the existing Autorun/Switch toolchain and Wine dependencies; this branch
does not vendor them. `work/build-wa2-complete.sh` records the tested CMake
options and the original container paths, which must be adapted to a new
machine. Build the loader with `work/wa2-complete-nsp/build.sh` in devkitPro.
Place a locally licensed white-background icon at
`work/wa2-complete-nsp/assets/icon-white.jpg`. Create the bundle with
`tools/make_bundle.py`, package with `packaging/make_nsp.py` using a **local**
header key, then run `work/validate-wa2-presentable.py`; exact arguments are
in [the project README](work/wa2-complete-nsp/README.md). Do not commit the
resulting inputs or outputs.

`WINE_NX_PACKAGE_ASSET=ON` requires the generated header and matching RomFS
assets. Without them, turn this experimental option off. The generator was
checked against the private 49-entry input: its output matched the header
used for the successful build byte-for-byte. This proves the mapping can be
regenerated, not that the proprietary inputs are available here.

## Validation evidence and limits

The complete direct-read prototype `0.3.0` was installed and exercised on
Switch hardware: first launch reported `PASS` after about 170 seconds of
resource verification; two cached launches logged about 857 and 887 ms for
the package check. All 49 expected package files were present in the installed
title, with no SD copies of those PAKs, and observed reads reached distinct
PAKs. The user also reported normal play, persistent game saves, and a much
faster second launch. The later `0.3.1` presentation package passed host-side
validation, but this record does **not** claim a new hardware test for it.
The displayed timing is the package check, not total time to the game menu.

Host checks can run without game content in a Linux toolchain with Python 3,
GCC, and OpenSSL/libcrypto:

```sh
cd experiments/wa2-nsp/work/wa2-complete-nsp
python3 -m unittest tests.test_bundle packaging.test_stream
sh tests/run-host.sh
cd ../package-cache-test
python3 run.py
```

The 7 GiB sparse-file layout test may need enough temporary filesystem
capacity even though it does not fill those extents with real game data.
Source attribution and license texts are documented in
[LICENSE-SOURCES.md](LICENSE-SOURCES.md).
