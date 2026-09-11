#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
vendor="$root/vendor/box64"
revision=dae0917c47b4edd8956f314210417a20fd225c4b
if [ ! -d "$vendor/.git" ]; then
    if [ -e "$vendor" ]; then
        echo "Refusing to overwrite $vendor" >&2
        exit 1
    fi
    mkdir -p "$vendor"
    git -C "$vendor" init -q
    git -C "$vendor" remote add origin https://github.com/ptitSeb/box64.git
    git -C "$vendor" fetch --depth=1 origin "$revision"
    git -C "$vendor" checkout -q --detach FETCH_HEAD
fi
if [ "$(git -C "$vendor" rev-parse HEAD)" != "$revision" ] ||
   [ -n "$(git -C "$vendor" status --porcelain)" ]; then
    echo "Box64 must be clean at $revision; no files were reset." >&2
    exit 1
fi
printf '%s\n' "$vendor"
