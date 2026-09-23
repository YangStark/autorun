#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
vendor="$root/vendor/libusbhsfs"
patches="$root/usbhsfs-uasp"
revision=625269b7725a6e2a3f2724e8d45b602c1b20ead5
if [ ! -d "$vendor/.git" ]; then
    if [ -e "$vendor" ]; then
        echo "Refusing to overwrite $vendor" >&2
        exit 1
    fi
    mkdir -p "$vendor"
    git -C "$vendor" init -q
    git -C "$vendor" remote add origin https://github.com/ITotalJustice/libusbhsfs.git
    git -C "$vendor" fetch --depth=1 origin "$revision"
    git -C "$vendor" checkout -q --detach FETCH_HEAD
fi
if [ "$(git -C "$vendor" rev-parse HEAD)" != "$revision" ]; then
    echo "libusbhsfs must be at $revision; no files were reset." >&2
    exit 1
fi
for patch in "$patches"/*.patch; do
    if tr -d '\r' < "$patch" | git -C "$vendor" apply --check - 2>/dev/null; then
        tr -d '\r' < "$patch" | git -C "$vendor" apply -
    elif ! tr -d '\r' < "$patch" | git -C "$vendor" apply --reverse --check - 2>/dev/null; then
        echo "$patch does not match libusbhsfs $revision" >&2
        exit 1
    fi
done
printf '%s\n' "$vendor"
