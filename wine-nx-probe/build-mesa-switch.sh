#!/bin/sh
# Build danfromtico/mesa-switch (Mesa 26 for Horizon: EGL and OpenGL through
# Gallium nvc0, and loaderless NVK Vulkan, over the port's own Horizon backend
# instead of libdrm_nouveau) into build-mesa-switch/install. OpenGL and Vulkan
# come from one Meson build, so the runtime links one copy of Mesa for both.
# WINE_NX_MESA_SWITCH_SRC is the checkout (default ~/mesa-switch);
# WINE_NX_MESA_IMAGE is the image mesa-switch's Docker.rust makes, with Meson,
# Rust nightly, bindgen and SPIRV-Tools (default devkitpro-mesa-rust:latest).
# Link the runtime with
# -DWINE_NX_MESA_SWITCH_DIR=<install>/opt/devkitpro/portlibs/switch/lib.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
src="${WINE_NX_MESA_SWITCH_SRC:-$HOME/mesa-switch}"
image="${WINE_NX_MESA_IMAGE:-devkitpro-mesa-rust:latest}"
out="$root/wine-nx-probe/build-mesa-switch"
lib="$out/install/opt/devkitpro/portlibs/switch/lib"
revision="$(git -C "$src" rev-parse HEAD)"
if [ -n "$(git -C "$src" status --porcelain --untracked-files=no)" ]; then
    echo "Refusing to build mesa-switch from a dirty checkout." >&2
    exit 1
fi

mkdir -p "$out"
docker run --rm --platform linux/arm64 -v "$src:/project" -v "$out:/out" -w /project "$image" bash -lc '
    set -e
    # What mesa-switch build-switch.sh sets up in its container: the cross
    # wrappers for bindgen and rustc, the Nouveau header NAK'"'"'s bindgen reads,
    # the Clang header path mesa_clc uses, and placeholder libdl, librt and
    # libutil for Rust std (rust_switch_stubs.c has the symbols).
    # Not its copy of the DRM winsys nouveau.h: the Switch OpenGL winsys is
    # written against devkitPro'"'"'s switch-libdrm_nouveau nouveau.h there
    # (nouveau_bo_get_syncpoint, the five-argument nouveau_device_new), and a
    # Vulkan-only build never compiles it. -p keeps the bindings from rebuilding.
    mkdir -p /usr/local/libexec
    cp bindgen-switch-wrapper.sh /usr/local/libexec/bindgen
    cp rustc-switch-wrapper.sh /usr/local/libexec/rustc
    cp bindgen-atomic-shim.h /usr/local/libexec/bindgen-atomic-shim.h
    sed -i "s/\r$//" /usr/local/libexec/bindgen /usr/local/libexec/rustc
    chmod +x /usr/local/libexec/bindgen /usr/local/libexec/rustc
    cp -p src/nouveau/headers/nv_device_info.h /opt/devkitpro/portlibs/switch/include/
    [ -d /usr/lib/llvm-15/lib/clang/15/include ] ||
        ln -sf /usr/lib/llvm-15/lib/clang/15.0.6/include /usr/lib/llvm-15/lib/clang/15/include
    if [ ! -f /opt/devkitpro/portlibs/switch/lib/libdl.a ]; then
        echo "void __mesa_switch_stub_lib(void) {}" > /tmp/stub_posix.c
        /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc -c /tmp/stub_posix.c -o /tmp/stub_posix.o
        for l in dl rt util; do
            /opt/devkitpro/devkitA64/bin/aarch64-none-elf-ar rcs /opt/devkitpro/portlibs/switch/lib/lib$l.a /tmp/stub_posix.o
        done
    fi

    # The host tools the cross build runs: the checkout'"'"'s own when
    # build-switch.sh made them, otherwise built here.
    tools=/project/builddir-native
    if [ ! -x $tools/src/compiler/clc/mesa_clc ] || [ ! -x $tools/src/compiler/spirv/vtn_bindgen2 ]; then
        tools=/out/native
        [ -f $tools/build.ninja ] || meson setup $tools \
            -Dvulkan-drivers= -Dgallium-drivers= -Dshader-cache=true -Dplatforms= \
            -Dglx=disabled -Degl=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
            -Dtools=[] -Dllvm=enabled -Dmesa-clc=enabled -Dprecomp-compiler=enabled -Dinstall-mesa-clc=true
        ninja -C $tools src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2
    fi
    export PATH="/usr/local/libexec:$tools/src/compiler/clc:$tools/src/compiler/spirv:$PATH"

    # build-opengl.sh'"'"'s EGL/OpenGL options with build-switch.sh'"'"'s NVK.
    [ -f /out/build/build.ninja ] || meson setup /out/build \
        --cross-file switch_cross_file.txt \
        --default-library=static \
        --prefix=/opt/devkitpro/portlibs/switch \
        --libdir=lib \
        --buildtype=release \
        -Doptimization=2 \
        -Db_lto=false \
        -Db_ndebug=true \
        -Dvulkan-drivers=nouveau \
        -Dgallium-drivers=nouveau \
        -Dgallium-rusticl=false \
        -Dplatforms=switch \
        -Degl-native-platform=switch \
        -Dglx=disabled \
        -Degl=enabled \
        -Dopengl=true \
        -Dgles1=enabled \
        -Dgles2=enabled \
        -Dvideo-codecs= \
        -Dshader-cache=enabled \
        -Dxmlconfig=enabled \
        -Dexpat=enabled \
        -Dtools=[] \
        -Dllvm=disabled \
        -Dshared-glapi=disabled \
        -Dshared-llvm=disabled \
        -Dmesa-clc=system \
        -Dprecomp-compiler=system \
        -Dcpp_rtti=false \
        -Dbuild-tests=false
    ninja -C /out/build
    rm -rf /out/install
    meson install -C /out/build --no-rebuild --destdir /out/install
    # nvk_archive_merge.py merges libvulkan.a with the image'"'"'s Debian ar
    # (binutils 2.40), which cannot read the Rust std members and leaves
    # their symbols out of the index, so ld then misses core and alloc.
    # devkitA64'"'"'s ranlib indexes every member.
    for a in /out/install/opt/devkitpro/portlibs/switch/lib/*.a; do
        /opt/devkitpro/devkitA64/bin/aarch64-none-elf-ranlib "$a"
    done
    # The runtime builds against the install alone, Vulkan headers included.
    inc=/out/install/opt/devkitpro/portlibs/switch/include
    [ -f $inc/vulkan/vulkan.h ] || { mkdir -p $inc; cp -r include/vulkan include/vk_video $inc/; }'
ls -la "$lib/libEGL.a" "$lib/libGL.a" "$lib/libglapi.a" "$lib/libvulkan.a"
printf '%s\n' "$revision" > "$out/source-revision.txt"
