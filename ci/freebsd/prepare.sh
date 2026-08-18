#!/bin/sh
set -eu

cd "$(dirname "$0")/../.."
export ASSUME_ALWAYS_YES=yes

PACKAGES="rsync git py312-sphinx"
PACKAGES_BUILD="scons-py312 pkgconf glib json-glib libblkid gettext"
PACKAGES_TEST="bash dash py312-pip"

echo "==> pkg install: $PACKAGES"

pkg install -y $PACKAGES
pkg install -y $PACKAGES_BUILD
pkg install -y $PACKAGES_TEST
pip install -r tests/requirements.txt
pip install -r docs/requirements.txt

mkdir /rt

echo "==> prepare done"
