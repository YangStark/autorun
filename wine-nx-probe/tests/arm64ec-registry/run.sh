#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build=${BUILD_DIR:-"$root/wine-nx-probe/toolchains/build-arm64ec-registry"}
cc=${CC:-cc}

mkdir -p "$build"
python3 "$here/extract_registry.py" "$root/wine-nx-probe/source/runtime.c" "$build/extracted_registry.inc"
"$cc" -std=gnu11 -O2 -g -Wall -Wextra -Werror -I"$build" \
    "$here/registry_test.c" -o "$build/arm64ec-registry"
exec "$build/arm64ec-registry"
