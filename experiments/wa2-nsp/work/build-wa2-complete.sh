set -eu
cmake -S /build/autorun-pergame-source/wine-nx-probe -B /build/runtime-wa2-complete -G Ninja -DCMAKE_TOOLCHAIN_FILE=/build/autorun-pergame-source/wine-nx-probe/cmake/switch-devkitA64.cmake -DCMAKE_BUILD_TYPE=Release -DWINE_NX_ROOT=/switch/autorun-games/wa2-full -DWINE_NX_PACKAGE_ASSET=ON -DWINE_NX_BOX64_DYNAREC=ON -DWINE_NX_LSFG=OFF -DWINE_NX_USB_STORAGE=OFF -DWINE_NX_AMD64=OFF -DWINE_NX_MESA_SWITCH_DIR=/build/mesa-output/install/opt/devkitpro/portlibs/switch/lib -DWINE_NX_PE_BUILD_DIR=/build/wine-headers
cmake --build /build/runtime-wa2-complete --target wine-nx-runtime-nro -j 4
