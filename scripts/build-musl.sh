#!/bin/sh
#
# Build a fully-static (musl) cwsd inside an Alpine container and package it.
# Intended to run *inside* alpine, e.g.:
#
#   docker run -i --rm -e ARCH=x86_64 -e VER=1.3.0 \
#       -v "$PWD":/src -w /src alpine:3.20 sh scripts/build-musl.sh
#
# Produces cwsd-<VER>-linux-<ARCH>-static.tar.gz in the work dir — a single
# dependency-free binary (see CWSD_FULLY_STATIC in CMakeLists.txt).
#
set -ex

: "${ARCH:=$(uname -m)}"
: "${VER:=dev}"
PREFIX="${PREFIX:-/opt/static}"

apk add --no-cache build-base cmake pkgconf git curl bash autoconf automake \
    libtool linux-headers libusb-dev tar >/dev/null

git config --global --add safe.directory "$PWD" 2>/dev/null || true

# Build the static dependency stack (skipped if already present — lets CI cache
# $PREFIX and lets local runs reuse it). musl needs the sys/types.h force-include
# for some hamlib backends (harris).
if [ ! -f "$PREFIX/lib/libhamlib.a" ]; then
    sh scripts/build-static-deps.sh "$PREFIX"
    EXTRA_CFLAGS="-include sys/types.h" HAMLIB_VERSION="${HAMLIB_VERSION:-4.7.2}" \
        sh scripts/build-hamlib-static.sh "$PREFIX"
fi

cmake -S . -B build-musl -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCWSD_FULLY_STATIC=ON -DHAMLIB_STATIC_ROOT="$PREFIX"
cmake --build build-musl -j"$(nproc)"

file build-musl/cwsd
file build-musl/cwsd | grep -q 'statically linked' || { echo "ERROR: not statically linked" >&2; exit 1; }

stage="cwsd-${VER}-linux-${ARCH}-static"
DESTDIR="$PWD/${stage}" cmake --install build-musl --strip
install -Dm644 README.md "${stage}/usr/share/doc/cwsd/README.md"
install -Dm644 LICENSE   "${stage}/usr/share/doc/cwsd/LICENSE"
tar -C "${stage}" -czf "${stage}.tar.gz" .
ls -lh "${stage}.tar.gz"
