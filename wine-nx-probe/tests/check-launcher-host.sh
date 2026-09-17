#!/bin/sh
# Host run of the launcher with SDL's dummy video driver: builds it with ASan,
# stages the programs of a built card as symlinks under "sdmc:", drives the
# screens with tests/launcher_host.c's script and checks what it wrote and
# returned. Screenshots go to $LAUNCHER_SHOTS when it is set.
# Needs Homebrew's sdl2 (sdl2-compat), sdl3, sdl2_ttf and libpng.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
probe="$root/wine-nx-probe"
drive_c="$probe/build-switch-wow64-dynarec/notepad-sd-card/switch/wine/drive_c"
build="$(mktemp -d "${TMPDIR:-/tmp}/wine-nx-launcher.XXXXXX")"
trap 'rm -rf "$build"' EXIT HUP INT TERM
font="${LAUNCHER_FONT:-/System/Library/Fonts/Supplemental/Arial.ttf}"
shots="${LAUNCHER_SHOTS:-$build}"

# The icons embedded in the launcher must match assets/, and fill as SVG does.
python3 "$probe/tools/make-launcher-icons.py" --check
clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$probe/tests/launcher_svg.c" "$probe/source/launcher_svg.c" -o "$build/launcher_svg"
"$build/launcher_svg"

clang -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$probe/source" $(sdl2-config --cflags) -I/opt/homebrew/include \
    "$probe/tests/launcher_host.c" "$probe/source/launcher.c" "$probe/source/launcher_catalog.c" "$probe/source/launcher_ui.c" \
    "$probe/source/steamgriddb.c" \
    "$probe/source/launcher_svg.c" \
    $(sdl2-config --libs) -L/opt/homebrew/lib -lSDL2_ttf -lpng -lcurl -o "$build/launcher_host"
# sdl2-compat looks for SDL3 next to the program, not in Homebrew's lib folder.
ln -s /opt/homebrew/lib/libSDL3.0.dylib "$build/libSDL3.dylib"

card="$build/card/sdmc:"
mkdir -p "$card/switch/wine/drive_c/openttd" "$card/games/deep/er/still"
ln -s "$drive_c/notepad.exe" "$drive_c/7zr.exe" "$card/switch/wine/drive_c/"
ln -s "$drive_c/notepad.exe" "$card/switch/wine/drive_c/openttd/openttd.exe"
ln -s "$drive_c/notepad.exe" "$card/games/deep/er/still/Deep.exe"

# An empty explicit catalog must stay empty even though drive_c contains several
# executables. Add OpenTTD through the browser, verify that adding did not launch
# it, then open its options with Y and start it from there.
cat > "$build/script.txt" <<SCRIPT
wait 10
shot $shots/library-empty.png
key x
wait 5
key a
wait 3
key a
wait 3
key a
wait 5
shot $shots/library-added.png
key y
wait 5
shot $shots/game-details.png
key a
SCRIPT

cd "$build/card"
SDL_VIDEODRIVER=dummy "$build/launcher_host" "$font" "$build/script.txt" > "$build/out.txt" 2>&1 || { cat "$build/out.txt"; exit 1; }
grep -q "launcher returned 1 target 'sdmc:/switch/wine/drive_c/openttd/openttd.exe'" "$build/out.txt" || { cat "$build/out.txt"; exit 1; }
grep -q '^version=3$' "sdmc:/switch/wine/launcher-library-v2.ini"
grep -q '^path=sdmc:/switch/wine/drive_c/openttd/openttd.exe$' "sdmc:/switch/wine/launcher-library-v2.ini"
test "$(grep -c '^\[game ' "sdmc:/switch/wine/launcher-library-v2.ini")" -eq 1
grep -qx "sdmc:/switch/wine/drive_c/openttd/openttd.exe" "sdmc:/switch/wine/target.txt"
grep "^\[LAUNCHER\]" "$build/out.txt"

# Restart from the saved catalog. Home must show the game that was played and A
# must start it without importing the other staged EXEs.
cat > "$build/restart-script.txt" <<SCRIPT
wait 5
key a
wait 3
key a
SCRIPT
SDL_VIDEODRIVER=dummy "$build/launcher_host" "$font" "$build/restart-script.txt" > "$build/restart-out.txt" 2>&1 || {
    cat "$build/restart-out.txt"; exit 1;
}
grep -q "1 catalog games (1 registered, 1 shown)" "$build/restart-out.txt" || { cat "$build/restart-out.txt"; exit 1; }
grep -q "launcher returned 1 target 'sdmc:/switch/wine/drive_c/openttd/openttd.exe'" "$build/restart-out.txt" || {
    cat "$build/restart-out.txt"; exit 1;
}
echo "launcher host run: empty home, explicit add, persistence, details and start passed"

# Eight played covers exercise Home's row: animated hit testing, swipe selection,
# both ends, the header, Y Options, and the square library.
cat > "$build/carousel-script.txt" <<SCRIPT
wait 35
key left
wait 20
shot $shots/carousel-first.png
key right
key right
wait 30
shot $shots/carousel-third.png
tap 400 250
wait 30
shot $shots/carousel-tap.png
swipe 750 250 450 250
wait 30
shot $shots/carousel-swipe.png
key right
key right
key right
key right
wait 30
shot $shots/carousel-last.png
key up
wait 10
shot $shots/carousel-header.png
key right
key right
key right
key a
wait 10
shot $shots/settings.png
key b
wait 10
key left
key left
key left
key down
key r
wait 15
shot $shots/carousel-library.png
key l
wait 20
key y
wait 10
shot $shots/carousel-options.png
key a
wait 5
key a
SCRIPT
SDL_VIDEODRIVER=dummy "$build/launcher_host" "$font" "$build/carousel-script.txt" --carousel-fixture > "$build/carousel-out.txt" 2>&1 || {
    cat "$build/carousel-out.txt"; exit 1;
}
grep -q "launcher returned 1 target 'sdmc:/switch/wine/drive_c/Vanguard/Game.exe'" "$build/carousel-out.txt" || {
    cat "$build/carousel-out.txt"; exit 1;
}
echo "launcher host run: Home history row, taps, swipes, boundaries, header focus and Y Options passed"
