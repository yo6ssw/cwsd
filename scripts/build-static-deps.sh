#!/usr/bin/env bash
#
# Build static libopus.a and libasound.a into a prefix, for CWSD_FULLY_STATIC
# (musl/Alpine) builds where the distro ships no static packages for them.
# libusb's static lib comes from Alpine's libusb-dev; hamlib from
# build-hamlib-static.sh — point all three at the SAME prefix.
#
# Usage:
#   scripts/build-static-deps.sh [INSTALL_PREFIX]
#
# Needs: build-base, autoconf/automake/libtool, curl, linux-headers (Alpine).
#
set -euo pipefail

OPUS_VERSION="${OPUS_VERSION:-1.5.2}"
ALSA_VERSION="${ALSA_VERSION:-1.2.12}"
PREFIX="${1:-${PREFIX:-$PWD/static-deps}}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

echo "==> opus ${OPUS_VERSION} (static)"
curl -fL "https://downloads.xiph.org/releases/opus/opus-${OPUS_VERSION}.tar.gz" -o opus.tar.gz
tar -xf opus.tar.gz
( cd "opus-${OPUS_VERSION}"
  ./configure --prefix="$PREFIX" --enable-static --disable-shared --with-pic \
      CFLAGS="-O2 -fPIC"
  make -j"$(nproc)"
  make install )

echo "==> alsa-lib ${ALSA_VERSION} (static)"
curl -fL "https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2" -o alsa.tar.bz2
tar -xf alsa.tar.bz2
( cd "alsa-lib-${ALSA_VERSION}"
  ./configure --prefix="$PREFIX" --enable-static --disable-shared --disable-python \
      --with-pic CFLAGS="-O2 -fPIC"
  make -j"$(nproc)"
  make install )

echo "==> done:"
ls -l "$PREFIX/lib/libopus.a" "$PREFIX/lib/libasound.a"
