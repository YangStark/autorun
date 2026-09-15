#!/bin/sh
# Build devkitPro's switch-mesa 20.1 and libdrm_nouveau with Wine-NX's changes
# (mesa/switch-mesa-20.1-wine-nx.patch, mesa/libdrm_nouveau-wine-nx.patch) into
# build-switch-mesa/install.
# WINE_NX_MESA_SRC is fincs' patched tree as the switch-mesa PKGBUILD leaves it,
# or a checkout of danfromtico/mesa-switch-legacy main, which already carries the
# Mesa patch (default ~/mesa-devkit/mesa-20.1.0-rc3); WINE_NX_LIBDRM_SRC is
# devkitPro's libdrm_nouveau or danfromtico/libdrm-nouveau-legacy main (default
# ~/libdrm_nouveau). A patch the source already carries is skipped.
# WINE_NX_MESA_IMAGE is a devkitPro image with meson, mako, flex and bison
# (default devkitpro-mesa-rust:latest).
# Link the runtime with -DWINE_NX_MESA_LIB_DIR=<install>/opt/devkitpro/portlibs/switch/lib.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
src="${WINE_NX_MESA_SRC:-$HOME/mesa-devkit/mesa-20.1.0-rc3}"
drm_src="${WINE_NX_LIBDRM_SRC:-$HOME/libdrm_nouveau}"
image="${WINE_NX_MESA_IMAGE:-devkitpro-mesa-rust:latest}"
out="$root/wine-nx-probe/build-switch-mesa"
lib="$out/install/opt/devkitpro/portlibs/switch/lib"

# Apply a patch unless the tree already carries it; a tree holding only part of
# it still stops the build.
apply_once() {
    if patch -d "$1" -p1 -R -f -s --dry-run < "$2" >/dev/null 2>&1; then
        echo "${2##*/} is already applied in $1"
    else
        patch -d "$1" -p1 -N < "$2"
    fi
}

mkdir -p "$out"
rsync -a --delete "$src/" "$out/src/"
apply_once "$out/src" "$root/wine-nx-probe/mesa/switch-mesa-20.1-wine-nx.patch"
apply_once "$out/src" "$root/wine-nx-probe/mesa/switch-mesa-explicit-flush.patch"
rsync -a --delete --exclude .git "$drm_src/" "$out/libdrm_nouveau/"
apply_once "$out/libdrm_nouveau" "$root/wine-nx-probe/mesa/libdrm_nouveau-wine-nx.patch"
apply_once "$out/libdrm_nouveau" "$root/wine-nx-probe/mesa/libdrm_nouveau-explicit-flush.patch"
docker run --rm --platform linux/arm64 -v "$out:/mesa" -w /mesa/src "$image" bash -lc '
    set -e
    [ -f ../build/build.ninja ] || /opt/devkitpro/meson-cross.sh switch ../crossfile.txt ../build -Db_ndebug=true
    ninja -C ../build
    rm -rf ../install
    DESTDIR=/mesa/install ninja -C ../build install
    make -C ../libdrm_nouveau lib/libdrm_nouveau.a
    cp ../libdrm_nouveau/lib/libdrm_nouveau.a /mesa/install/opt/devkitpro/portlibs/switch/lib/'
ls -la "$lib/libEGL.a" "$lib/libglapi.a" "$lib/libdrm_nouveau.a"
