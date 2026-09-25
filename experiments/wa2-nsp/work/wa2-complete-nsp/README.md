# White Album 2 0.3.1

Homebrew NSP source tree for title `0500A17E00070000`. The application root is
`/switch/autorun-games/wa2-full`; the target is
`drive_c/WA2/WA2_full_menu.exe`. The Program RomFS carries the runtime bundle
and 49 PAK files directly. No PAK file is permitted in the runtime bundle.
The loader deploys the bundle's small, immutable files and preserves the
existing seed configuration behavior. The tested HOME icon uses a snowflake
from WA2_full_menu.exe on a white background. Supply your own licensed
`assets/icon-white.jpg` locally; that image is not included in this repository.

Build the loader with `./build.sh` in the devkitPro environment. Build the
bundle from the prepared PAK-free payload using:

```
python tools/make_bundle.py --payload ../wa2-complete-payload \
  --output build/bundle.bin --manifest build/payload-manifest.json
```

Package with `packaging/make_nsp.py --main build/exefs/main --npdm
build/exefs/main.npdm --icon build/icon.jpg --bundle build/bundle.bin
--assets-manifest ../wa2-full-assets.json --asset-root ../wa2-game-clean
--header-key <local-key-file> --atmosphere 1.6.1 --output <output.nsp>`.
The manifest order defines the RomFS package name table. The packager checks
paths and file bounds, then streams all assets without reading a PAK into RAM.
Run `python ../validate-wa2-presentable.py` after packaging to validate CNMT,
NPDM, the HOME name and icon, bundle contents, and every PAK hash.

This example is fixed to WA2. The game assets, prepared payload, manifest,
icon, keys, binaries, and NSP are private local inputs, not repository files.
For the workflow and licensing notes, see `../../README.md`.
