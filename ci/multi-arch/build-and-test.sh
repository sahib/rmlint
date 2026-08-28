#!/bin/sh
set -eu

CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"

cd "$(dirname "$0")/../.."

arch="$1"
arch_plat() {
    case "$arch" in
        amd64)  echo 'linux/amd64 amd64' ;;
        i386)   echo 'linux/386 i386' ;;
        armv7)  echo 'linux/arm/v7 arm32v7' ;;
        arm64)  echo 'linux/arm64/v8 arm64v8' ;;
        s390x)  echo 'linux/s390x s390x' ;;
        *)
            echo "unknown architecture" >&2
            exit 2 ;;
    esac
}

ap=$(arch_plat "$arch")
platform=${ap% *}
image="docker.io/${ap#* }/debian:testing"
image_tag="rmlint-ci:$arch"

echo "==> $arch: build"
if [ "$CONTAINER_ENGINE" = "docker" ]; then
    builder="docker build"
else
    builder="buildah build --layers"
fi

if ! $builder \
        --platform "$platform" \
        --build-arg "DEB_IMAGE=$image" \
        -f ci/multi-arch/Containerfile \
        -t "$image_tag" \
        .
then
    echo "==> $arch: build failed"
    exit 1
fi
echo "==> $arch: build OK"

echo "==> $arch: Test"
if ! "$CONTAINER_ENGINE" run --rm \
        --platform "$platform" \
        --tmpfs /tmp:rw,exec,mode=1777 \
        "$image_tag"
then
    echo "==> $arch: tests failed"
    exit 1
fi
echo "==> $arch: tests passed"
