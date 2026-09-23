#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build=${BUILD_DIR:-"$root/wine-nx-probe/toolchains/build-arm64ec-transitions"}
cc=${CC:-cc}

test "$(uname -m)" = aarch64
mkdir -p "$build"
python3 "$here/extract_transitions.py" "$root/dlls/winebox64ec/cpu.c" "$build/transitions.S"
"$cc" -std=c11 -O2 -g -Wall -Wextra -Werror -ffixed-x18 -fno-pie -no-pie \
    "$build/transitions.S" "$here/stubs.S" "$here/transitions_test.c" \
    -o "$build/arm64ec-transitions"
exec "$build/arm64ec-transitions"
