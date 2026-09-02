#!/bin/sh
set -eu

cd "$(dirname "$0")/../.."
export ASSUME_ALWAYS_YES=yes

PACKAGES="rsync git python3"
PACKAGES_BUILD="scons-py312 pkgconf gettext py312-sphinx glib json-glib libblkid"
PACKAGES_TEST="py312-pip bash dash"

echo "==> pkg install: $PACKAGES"

pkg install -y $PACKAGES
pkg install -y $PACKAGES_BUILD
pkg install -y $PACKAGES_TEST
pip install -r tests/requirements.txt
pip install -r docs/requirements.txt

mkdir /rt

echo "==> prepare done"
