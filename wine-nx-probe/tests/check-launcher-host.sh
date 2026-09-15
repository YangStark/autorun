#!/bin/sh
# Host run of the launcher with SDL's dummy video driver: builds it with ASan,
# stages the programs of a built card as symlinks under "sdmc:", drives the
# screens with tests/launcher_host.c's script and checks what it wrote and
# returned. Screenshots go to $LAUNCHER_SHOTS when it is set.
# Needs Homebrew's sdl2 (sdl2-compat), sdl3, sdl2_ttf and libpng.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
probe="$root/wine-nx-probe"
drive_c="$probe/build-switch-wow64-dynarec/switch/wine/drive_c"
build="$(mktemp -d "${TMPDIR:-/tmp}/wine-nx-launcher.XXXXXX")"
trap 'rm -rf "$build"' EXIT HUP INT TERM
font="${LAUNCHER_FONT:-/System/Library/Fonts/Supplemental/Arial.ttf}"
shots="${LAUNCHER_SHOTS:-$build}"

clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$probe/source" $(sdl2-config --cflags) -I/opt/homebrew/include \
    "$probe/tests/launcher_host.c" "$probe/source/launcher.c" "$probe/source/launcher_ui.c" \
    $(sdl2-config --libs) -L/opt/homebrew/lib -lSDL2_ttf -lpng -o "$build/launcher_host"
# sdl2-compat looks for SDL3 next to the program, not in Homebrew's lib folder.
ln -s /opt/homebrew/lib/libSDL3.0.dylib "$build/libSDL3.dylib"

card="$build/card/sdmc:"
mkdir -p "$card/switch/wine/drive_c/openttd" "$card/games/deep/er/still"
ln -s "$drive_c/notepad.exe" "$drive_c/7zr.exe" "$card/switch/wine/drive_c/"
ln -s "$drive_c/openttd/openttd.exe" "$card/switch/wine/drive_c/openttd/"
ln -s "$drive_c/notepad.exe" "$card/games/deep/er/still/Deep.exe"

# Library order is by title: 7-Zip..., notepad, OpenTTD. Open OpenTTD's menu,
# give it arguments, verbose traces on, hide it (the last row, up from Start); add Deep.exe from the file
# browser, then start it from the library, where it sits left of notepad.
cat > "$build/script.txt" <<SCRIPT
wait 10
shot $shots/library.png
key right
key right
key y
wait 5
shot $shots/program.png
key down
key down
type -s null -m null
key a
key down
key right
key up
key up
key up
key up
wait 5
shot $shots/program-edited.png
key a
key b
key minus
wait 5
key b
key b
key b
key up
key a
key a
key a
key a
key a
key up
key a
wait 5
shot $shots/added.png
key b
key b
key b
key b
key b
key b
wait 5
shot $shots/library-after.png
key left
key a
SCRIPT

cd "$build/card"
SDL_VIDEODRIVER=dummy "$build/launcher_host" "$font" "$build/script.txt" > "$build/out.txt" 2>&1 || { cat "$build/out.txt"; exit 1; }
grep -q "launcher returned 1 target 'sdmc:/games/deep/er/still/Deep.exe'" "$build/out.txt" || { cat "$build/out.txt"; exit 1; }
grep -qx -- "-s null -m null" "sdmc:/switch/wine/drive_c/openttd/openttd.args.txt"
grep -qx "verbose=1" "sdmc:/switch/wine/drive_c/openttd/openttd.wine-nx.txt"
grep -qx "hidden=1" "sdmc:/switch/wine/drive_c/openttd/openttd.wine-nx.txt"
grep -qx "sdmc:/games/deep/er/still/Deep.exe" "sdmc:/switch/wine/launcher-library.txt"
grep -qx "sdmc:/games/deep/er/still/Deep.exe" "sdmc:/switch/wine/target.txt"
grep "^\[LAUNCHER\]" "$build/out.txt"
echo "launcher host run: library, program settings, file browser, adding and starting passed"
