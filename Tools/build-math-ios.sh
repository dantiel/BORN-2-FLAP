#!/usr/bin/env bash
# Cross-compile the canonical Haskell math core to an iOS static library.
#
# Produces libborn2flap_math-ios.a: a self-contained arm64-apple-ios archive
# (RTS + base + born2flap-math bundled) exposing the same C ABI as the desktop
# dylib (b2f_math_* / hs_b2f_math_*, plus hs_init). This is the iOS link form:
# mobile platforms forbid dlopen of third-party dylibs, so the whole GHC
# runtime is statically linked into the app binary.
#
# LINK REQUIREMENT: when this archive is linked into the app, add `-liconv`
# (GHC's Iconv text-encoding codec pulls iconv_open/iconv/iconv_close). Nothing
# else is needed — the ghc-ios RTS already carries iOS-correct
# mprotectForLinker (no-op under W^X) and osReserveHeapMemory, so no RTS
# link-time overrides are required. Verified: a minimal main() links to a
# genuine `platform IOS, minos 15.0` binary with just `-liconv`.
#
# The compiler is the Novavero-AI/ghc-ios toolchain — a GHC 9.14.1 cross
# compiler for aarch64-apple-ios (BSD-3-Clause). It ships as a prebuilt tarball:
#   https://github.com/Novavero-AI/ghc-ios/releases
# Install it once (242 MB), then this script needs only Xcode + its iOS SDK:
#   mkdir -p ~/ghc-ios && curl -L <release-url> | tar xz -C ~/ghc-ios --strip-components=1
#
# Usage:
#   Tools/build-math-ios.sh [--ghc=/path/to/aarch64-apple-ios-ghc]
#   (default toolchain: ~/ghc-ios/bin/aarch64-apple-ios-ghc)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT/MathCore"

GHC="${GHC_IOS:-$HOME/ghc-ios/bin/aarch64-apple-ios-ghc}"
OUT="$REPO_ROOT/MathCore/dist-newstyle/libborn2flap_math-ios.a"
while [ $# -gt 0 ]; do
    case "$1" in
        --ghc=*) GHC="${1#*=}" ;;
        --out=*) OUT="${1#*=}" ;;
        *) echo "usage: $0 [--ghc=path] [--out=path]" >&2; exit 2 ;;
    esac
    shift
done

if [ ! -x "$GHC" ]; then
    echo "error: iOS cross GHC not found at: $GHC" >&2
    echo "       install from https://github.com/Novavero-AI/ghc-ios/releases" >&2
    exit 1
fi

# GHC appends `.a` to the -o basename when no extension is given.
OUT_BASE="${OUT%.a}"

echo "== Cross-compiling born2flap-math -> iOS static library =="
echo "   GHC:   $GHC"
echo "   target: aarch64-apple-ios"
"$GHC" -staticlib -no-hs-main -isrc -icbits -O2 \
    src/Born2Flap/Math/FFI.hs cbits/bridge.c -o "$OUT_BASE"

if [ "$OUT_BASE.a" != "$OUT" ]; then
    mv -f "$OUT_BASE.a" "$OUT"
fi

echo "== Done: $OUT =="
xcrun --sdk iphoneos nm -gU "$OUT" | grep -c "b2f_math" | xargs echo "   exported b2f_math symbols:"