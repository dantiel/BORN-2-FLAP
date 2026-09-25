#!/usr/bin/env bash
# Package BORN2FLAP for macOS into a distributable .dmg.
# Builds the arm64 Haskell math library, cooks a Shipping build via RunUAT,
# stages the dylib beside the binary, and wraps the .app in a .dmg.
#
# Usage:
#   Tools/package-mac.sh [tag]
#   UE_ROOT=/path/to/UE_5.8 OUTDIR=/tmp/out Tools/package-mac.sh v0.1.0
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# --- Locate Unreal Engine -----------------------------------------------------
UE_ROOT="${UE_ROOT:-}"
if [ -z "$UE_ROOT" ]; then
    for candidate in \
        "/Volumes/Ouranos/Games/UE_5.8" \
        "/Users/Shared/Epic Games/UE_5.8" \
        "/Applications/Epic Games/UE_5.8.app/Contents"; do
        if [ -f "$candidate/Engine/Build/BatchFiles/RunUAT.sh" ]; then
            UE_ROOT="$candidate"
            break
        fi
    done
fi
if [ -z "$UE_ROOT" ] || [ ! -f "$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" ]; then
    echo "error: Unreal Engine 5.8 not found. Set UE_ROOT=/path/to/UE_5.8" >&2
    exit 1
fi
echo "UE_ROOT=$UE_ROOT"

PROJECT="$REPO_ROOT/Unreal/Born2Flap/Born2Flap.uproject"
TAG="${1:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}"
OUTDIR="${OUTDIR:-$REPO_ROOT/build/release/$TAG}"
mkdir -p "$OUTDIR"

# --- Haskell math library (arm64) ---------------------------------------------
# This Mac's shell runs under Rosetta (x86_64) but the editor + existing dylib
# are native arm64. Force arm64 for the cabal build when on Apple Silicon.
if [ "$(uname -m)" = "x86_64" ] && [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = "1" ]; then
    ARCH_PREFIX="arch -arm64"
else
    ARCH_PREFIX=""
fi

echo "== Building Haskell math library (arm64) =="
export PATH="$HOME/.ghcup/bin:$PATH"
( cd MathCore && $ARCH_PREFIX cabal build flib:born2flap_math )

DYLIB="$(find MathCore/dist-newstyle -name 'libborn2flap_math.dylib' -type f | head -1)"
if [ -z "$DYLIB" ]; then
    echo "error: built libborn2flap_math.dylib not found" >&2
    exit 1
fi
mkdir -p Unreal/Born2Flap/Binaries/ThirdParty
cp -f "$DYLIB" Unreal/Born2Flap/Binaries/ThirdParty/libborn2flap_math.dylib
echo "staged $DYLIB"

# --- Cook + package (Shipping) --------------------------------------------------
echo "== Packaging (Shipping) =="
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
    -project="$PROJECT" \
    -noP4 -utf8output -unattended -nocompileeditor \
    -platform=Mac \
    -clientconfig=Shipping -serverconfig=Shipping \
    -build -cook -stage -pak -archive \
    -archivedirectory="$OUTDIR"

# --- Stage the Haskell lib into the package --------------------------------------
# The bridge resolves ProjectDir()/Binaries/ThirdParty/<lib> at runtime, so the
# library must sit next to the packaged .uproject (Unreal ships a copy of it).
PKG_PROJECT_DIR="$(dirname "$(find "$OUTDIR" -name 'Born2Flap.uproject' -type f | head -1)")"
if [ -z "$PKG_PROJECT_DIR" ] || [ "$PKG_PROJECT_DIR" = "." ]; then
    PKG_PROJECT_DIR="$(find "$OUTDIR" -name 'Born2Flap.app' -type d | head -1)/Contents"
fi
if [ -n "$PKG_PROJECT_DIR" ] && [ -d "$PKG_PROJECT_DIR" ]; then
    mkdir -p "$PKG_PROJECT_DIR/Binaries/ThirdParty"
    cp -f Unreal/Born2Flap/Binaries/ThirdParty/libborn2flap_math.dylib \
        "$PKG_PROJECT_DIR/Binaries/ThirdParty/"
    echo "staged Haskell lib into $PKG_PROJECT_DIR/Binaries/ThirdParty/"
else
    echo "warning: could not locate packaged project dir to stage the dylib" >&2
fi

# --- Wrap into .dmg ---------------------------------------------------------------
APP="$(find "$OUTDIR" -name 'Born2Flap.app' -type d | head -1)"
if [ -z "$APP" ]; then
    echo "error: Born2Flap.app not found after packaging" >&2
    exit 1
fi
DMG="$OUTDIR/BORN2FLAP-$TAG-mac.dmg"
hdiutil create -volname "BORN2FLAP" -srcfolder "$APP" -ov -format UDZO "$DMG"
echo "== Done: $DMG =="
