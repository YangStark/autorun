#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build="$(mktemp -d "${TMPDIR:-/tmp}/wine-nx-wow64-unix.XXXXXX")"
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CC:-clang}" -std=gnu11 -fms-extensions -Wall -Wextra -Werror \
    -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer \
    -D__WINESRC__ -D_WIN64 -DWINE_UNIX_LIB \
    -I"$root/include" -I"$root/wine-nx-probe/source" \
    "$root/wine-nx-probe/source/wow64_box64_unix.c" \
    "$root/wine-nx-probe/tests/wow64_box64_unix.c" -o "$build/check"
"$build/check"
# This bridge needs no libnx APIs; compile its Switch diagnostics on the host too.
"${CC:-clang}" -std=gnu11 -fms-extensions -Wall -Wextra -Werror \
    -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer \
    -D__WINESRC__ -D_WIN64 -DWINE_UNIX_LIB -D__SWITCH__ \
    -I"$root/include" -I"$root/wine-nx-probe/source" \
    "$root/wine-nx-probe/source/wow64_box64_unix.c" \
    "$root/wine-nx-probe/tests/wow64_box64_unix.c" -o "$build/check-switch"
"$build/check-switch"
