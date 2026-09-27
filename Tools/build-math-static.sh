#!/usr/bin/env bash
# Build the self-contained STATIC math library (RTS + base + math bundled).
#
# This is the iOS link form: mobile platforms forbid dlopen of third-party
# dylibs, so the whole GHC runtime must be statically linked into the app
# binary. The shared build (flib:born2flap_math) remains the desktop form.
#
# cabal cannot emit native-static foreign libraries on macOS
# ("We can currently only build shared foreign libraries on OSX"), so this
# script drives GHC's -staticlib flag directly. It produces libborn2flap_math.a
# with the same C ABI symbols as the shared build (b2f_math_*, hs_b2f_math_*).
#
# Usage:
#   Tools/build-math-static.sh [--target=aarch64-apple-ios] [--out=path]
#   (cross targets additionally need a cross GHC + SDK/NDK toolchain)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT/MathCore"

TARGET=""
OUT="$REPO_ROOT/MathCore/dist-newstyle/libborn2flap_math.a"
while [ $# -gt 0 ]; do
    case "$1" in
        --target=*) TARGET="${1#*=}" ;;
        --out=*)    OUT="${1#*=}" ;;
        *) echo "usage: $0 [--target=triple] [--out=path]" >&2; exit 2 ;;
    esac
    shift
done

export PATH="$HOME/.ghcup/bin:$PATH"

GHC_ARGS=(-staticlib -isrc -icbits -O2)
if [ -n "$TARGET" ]; then
    GHC_ARGS+=("-target" "$TARGET")
fi

# GHC appends `.a` to the -o basename when no extension is given.
OUT_BASE="${OUT%.a}"
echo "== Building static math library (${TARGET:-host}) =="
ghc "${GHC_ARGS[@]}" src/Born2Flap/Math/FFI.hs cbits/bridge.c -o "$OUT_BASE"
mkdir -p "$(dirname "$OUT")"
if [ "$OUT_BASE.a" != "$OUT" ]; then
    mv -f "$OUT_BASE.a" "$OUT"
fi
echo "== Done: $OUT =="
