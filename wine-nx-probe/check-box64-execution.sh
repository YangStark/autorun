#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
sh "$root/wine-nx-probe/tools/bootstrap-box64-core.sh"
docker run --rm --platform linux/arm64 -v "$root:/work" -w /work \
    devkitpro/devkita64 sh -ec '
    cmake -S wine-nx-probe/tests/box64-core -B wine-nx-probe/build-box64-core -G Ninja
    cmake --build wine-nx-probe/build-box64-core -j 8
    ctest --test-dir wine-nx-probe/build-box64-core --output-on-failure
    cmake -S wine-nx-probe/tests/box64-core -B wine-nx-probe/build-box64-core/switch -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/work/wine-nx-probe/cmake/switch-devkitA64.cmake
    cmake --build wine-nx-probe/build-box64-core/switch -j 8
    '
