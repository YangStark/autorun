#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build=${BUILD_DIR:-"$root/wine-nx-probe/toolchains/build-arm64ec-thread-init"}
cc=${CC:-cc}

mkdir -p "$build"
python3 "$here/extract_thread_context.py" "$root/dlls/winebox64ec/cpu.c" \
    "$build/extracted_thread_context.inc"
"$cc" -std=c11 -O2 -g -Wall -Wextra -Werror -I"$build" \
    "$here/thread_context_test.c" -o "$build/arm64ec-thread-init"
exec "$build/arm64ec-thread-init"
