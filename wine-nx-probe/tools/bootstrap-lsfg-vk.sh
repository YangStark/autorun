#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
vendor="$root/vendor/lsfg-vk"
revision="$(cat "$root/lsfg/revision.txt")"
patch="$root/lsfg/horizon.patch"
if [ ! -d "$vendor/.git" ]; then
    if [ -e "$vendor" ]; then
        echo "Refusing to overwrite $vendor" >&2
        exit 1
    fi
    mkdir -p "$vendor"
    git -C "$vendor" init -q
    git -C "$vendor" remote add origin https://git.lsfg-vk.dev/lsfg-vk-archive.git
    git -C "$vendor" -c core.autocrlf=false fetch --depth=1 origin "$revision"
    git -C "$vendor" -c core.autocrlf=false checkout -q --detach FETCH_HEAD
fi
if [ "$(git -C "$vendor" rev-parse HEAD)" != "$revision" ]; then
    echo "LSFG-VK must be at $revision; no files were reset." >&2
    exit 1
fi
if git -C "$vendor" apply --reverse --check "$patch" 2>/dev/null; then
    :
elif git -C "$vendor" diff --quiet && git -C "$vendor" diff --cached --quiet; then
    git -C "$vendor" apply --check "$patch"
    git -C "$vendor" apply "$patch"
else
    echo "LSFG-VK has unexpected changes; no files were reset." >&2
    exit 1
fi
printf '%s\n' "$vendor"
