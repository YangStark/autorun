#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export DEVKITPRO=/opt/devkitpro
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
mkdir -p build/exefs
cc=aarch64-none-elf-gcc
flags=(-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -O2 -g -Wall -Wextra -ffunction-sections -fdata-sections -D__SWITCH__ -I/opt/devkitpro/libnx/include -Iloader)
link=(-specs=/opt/devkitpro/libnx/switch.specs -L/opt/devkitpro/libnx/lib -Wl,--gc-sections)
nacptool --create 'White Album 2' YangStark 0.3.1 build/control.nacp
"$cc" "${flags[@]}" -Wno-unused-parameter -DVERSION='"wa2-full-0.3.1"' loader/main.c loader/progress.c loader/deploy.c loader/trampoline.s "${link[@]}" -Wl,-wrap,exit -lnx -o build/loader.elf
elf2nso build/loader.elf build/exefs/main
npdmtool loader/hbl.json build/exefs/main.npdm
cp assets/icon-white.jpg build/icon.jpg
echo 'Loader NSO built; package only after the runtime payload bundle is staged.'
