#!/bin/sh
# Build devkitPro's switch-mesa 20.1 and libdrm_nouveau with Wine-NX's changes
# (mesa/switch-mesa-20.1-wine-nx.patch, mesa/libdrm_nouveau-wine-nx.patch) into
# build-switch-mesa/install.
# WINE_NX_MESA_SRC is fincs' patched tree as the switch-mesa PKGBUILD leaves it
# (default ~/mesa-devkit/mesa-20.1.0-rc3); WINE_NX_LIBDRM_SRC is devkitPro's
# libdrm_nouveau (default ~/libdrm_nouveau). WINE_NX_MESA_IMAGE is a devkitPro
# image with meson, mako, flex and bison (default devkitpro-mesa-rust:latest).
# Link the runtime with -DWINE_NX_MESA_LIB_DIR=<install>/opt/devkitpro/portlibs/switch/lib.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
src="${WINE_NX_MESA_SRC:-$HOME/mesa-devkit/mesa-20.1.0-rc3}"
drm_src="${WINE_NX_LIBDRM_SRC:-$HOME/libdrm_nouveau}"
image="${WINE_NX_MESA_IMAGE:-devkitpro-mesa-rust:latest}"
out="$root/wine-nx-probe/build-switch-mesa"
lib="$out/install/opt/devkitpro/portlibs/switch/lib"

mkdir -p "$out"
rsync -a --delete "$src/" "$out/src/"
patch -d "$out/src" -p1 < "$root/wine-nx-probe/mesa/switch-mesa-20.1-wine-nx.patch"
rsync -a --delete --exclude .git "$drm_src/" "$out/libdrm_nouveau/"
patch -d "$out/libdrm_nouveau" -p1 < "$root/wine-nx-probe/mesa/libdrm_nouveau-wine-nx.patch"
docker run --rm --platform linux/arm64 -v "$out:/mesa" -w /mesa/src "$image" bash -lc '
    set -e
    [ -f ../build/build.ninja ] || /opt/devkitpro/meson-cross.sh switch ../crossfile.txt ../build -Db_ndebug=true
    ninja -C ../build
    rm -rf ../install
    DESTDIR=/mesa/install ninja -C ../build install
    make -C ../libdrm_nouveau lib/libdrm_nouveau.a
    cp ../libdrm_nouveau/lib/libdrm_nouveau.a /mesa/install/opt/devkitpro/portlibs/switch/lib/'
ls -la "$lib/libEGL.a" "$lib/libglapi.a" "$lib/libdrm_nouveau.a"
