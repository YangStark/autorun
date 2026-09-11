#!/bin/sh
# Host tests for console programs: the runtime's copy of stdout/stderr into its
# log, and Horizon file access (rights, descriptor modes, open-file path matching).
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build="$(mktemp -d "${TMPDIR:-/tmp}/wine-nx-console.XXXXXX")"
trap 'rm -rf "$build"' EXIT HUP INT TERM
flags="-std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"
"${CC:-clang}" $flags "$root/wine-nx-probe/tests/std_stream_lines.c" -o "$build/std_stream_lines"
"$build/std_stream_lines"
"${CC:-clang}" $flags -I"$root/include" "$root/wine-nx-probe/tests/horizon_file_access.c" -o "$build/file_access"
"$build/file_access"
python3 "$root/wine-nx-probe/tools/make-7zr-tree.py" "$build/drive_c" >/dev/null
