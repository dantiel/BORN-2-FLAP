#!/usr/bin/env bash
# Cross-compile Oniguruma (the regex engine behind mruby-onig-regexp) to a
# static arm64-apple-ios library for the in-process Ruby Brain.
#
# Produces ThirdParty/oniguruma/dist/lib/libonig.a + dist/include/oniguruma.h.
# mruby's iOS cross-build links against this instead of the macOS host
# libonig (which is x86_64/arm64-macos and cannot be linked into an iOS app).
#
# Version pinned to 6.9.10 to match the host build (brew oniguruma 6.9.10),
# so the Brain behaves identically on desktop and device.
#
# Usage:
#   Tools/build-onig-ios.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO_ROOT/ThirdParty/oniguruma"
OUT="$SRC/dist"
ONIG_VER="6.9.10"
TARBALL="$REPO_ROOT/ThirdParty/onig-$ONIG_VER.tar.gz"

if [ ! -d "$SRC" ] || [ ! -f "$SRC/configure" ]; then
    echo "== Fetching oniguruma $ONIG_VER =="
    curl -sL -o "$TARBALL" \
        "https://github.com/kkos/oniguruma/releases/download/v$ONIG_VER/onig-$ONIG_VER.tar.gz"
    rm -rf "$SRC"
    tar xzf "$TARBALL" -C "$REPO_ROOT/ThirdParty"
    mv "$REPO_ROOT/ThirdParty/onig-$ONIG_VER" "$SRC"
fi

SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
MIN_IOS="${B2F_MIN_IOS:-15.0}"

export CC="xcrun -sdk iphoneos clang -arch arm64 -target arm64-apple-ios"
export CFLAGS="-isysroot $SDKROOT -miphoneos-version-min=$MIN_IOS -O2"
export CPP="xcrun -sdk iphoneos clang -E"

echo "== Cross-compiling oniguruma $ONIG_VER -> arm64-apple-ios =="
echo "   SDK: $SDKROOT"

cd "$SRC"
make distclean >/dev/null 2>&1 || true
./configure \
    --build="$(uname -m)-apple-darwin" \
    --host=arm64-apple-ios \
    --disable-shared \
    --enable-static \
    --prefix="$OUT" \
    >/dev/null

make -j"$(sysctl -n hw.ncpu)" >/dev/null
make install >/dev/null

echo "== Done: $OUT =="
ls -la "$OUT/lib/libonig.a" "$OUT/include/oniguruma.h"
xcrun --sdk iphoneos nm -gU "$OUT/lib/libonig.a" 2>/dev/null | grep -c "onig_" | xargs echo "   exported onig_ symbols:"
