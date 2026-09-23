#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build=${BUILD_DIR:-"$root/wine-nx-probe/toolchains/build-arm64ec-dispatchers"}
cc=${CC:-cc}

test "$(uname -m)" = aarch64
mkdir -p "$build"
python3 "$here/extract_dispatchers.py" "$root/dlls/ntdll/unix/signal_arm64.c" "$build/dispatchers.S"
"$cc" -std=c11 -O2 -g -Wall -Wextra -Werror -fno-pie -no-pie \
    "$build/dispatchers.S" "$here/stubs.S" "$here/dispatchers_test.c" \
    -o "$build/arm64ec-dispatchers"
exec "$build/arm64ec-dispatchers"
