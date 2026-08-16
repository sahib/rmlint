#!/bin/sh
set -eu

cd "$(dirname "$0")/../.."
echo "==> $(freebsd-version) $(uname -m), $(sysctl -n hw.ncpu) cpus, $(pwd)"

scons_ARGS="VERBOSE=1 DEBUG=1 O=release"
scons config $scons_ARGS
scons $scons_ARGS

mount -t tmpfs none /rt
RM_TS_DIR=/rt pytest -m "not slow"

echo "==> tests passed"
