#!/bin/sh
# build the container from the repository root with:
# buildah bud --layers --target run-gui -t rmlint/run-gui .
set -eu

: "${XDG_RUNTIME_DIR:?XDG_RUNTIME_DIR is not set}"
: "${WAYLAND_DISPLAY:?WAYLAND_DISPLAY is not set}"

image=${IMAGE:-rmlint/run-gui}
uid=$(id -u)

exec podman run --rm \
    --tmpfs /tmp:rw,nosuid,nodev,mode=1777 \
    --mount type=tmpfs,destination=/run/user/${uid},tmpfs-mode=700 \
    -e WAYLAND_DISPLAY \
    -e XDG_RUNTIME_DIR=/run/user/${uid} \
    -e XDG_SESSION_TYPE=wayland \
    -e GDK_BACKEND=wayland \
    -e GDK_SCALE \
    -v "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}:/run/user/${uid}/${WAYLAND_DISPLAY}:ro" \
    --device /dev/dri \
    "$@" \
    "$image"
