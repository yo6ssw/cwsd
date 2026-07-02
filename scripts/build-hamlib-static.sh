#!/usr/bin/env bash
#
# Build a static libhamlib.a for linking a portable cwsd (see CWSD_STATIC_HAMLIB
# in CMakeLists.txt and .github/workflows/portable.yml).
#
# Usage:
#   scripts/build-hamlib-static.sh [INSTALL_PREFIX]
#   HAMLIB_VERSION=4.7.2 scripts/build-hamlib-static.sh /opt/hamlib-static
#
# Needs: build-essential, curl, and libusb-1.0-0-dev (for USB rig backends).
# Produces: $PREFIX/lib/libhamlib.a and $PREFIX/include/hamlib/…
#
set -euo pipefail

HAMLIB_VERSION="${HAMLIB_VERSION:-4.7.2}"
PREFIX="${1:-${PREFIX:-$PWD/hamlib-static}}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

url="https://github.com/Hamlib/Hamlib/releases/download/${HAMLIB_VERSION}/hamlib-${HAMLIB_VERSION}.tar.gz"
echo "==> downloading hamlib ${HAMLIB_VERSION}"
curl -fL "$url" -o "$work/hamlib.tar.gz"
tar -xzf "$work/hamlib.tar.gz" -C "$work"

cd "$work/hamlib-${HAMLIB_VERSION}"
echo "==> configuring (static, --without-readline)"
# -fPIC so the archive can be linked into any executable; readline is only used by
# the rigctl* tools (GPL) which we don't build — dropping it keeps the archive
# LGPL-clean and lean.
./configure \
    --prefix="$PREFIX" \
    --enable-static --disable-shared \
    --without-readline \
    CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC"

echo "==> building"
make -j"$(nproc)"
make install

echo "==> done:"
ls -l "$PREFIX/lib/libhamlib.a"
