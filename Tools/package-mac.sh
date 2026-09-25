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
# Force the SAME arch as the Haskell lib (arm64) so the packaged .app can
# dlopen the dylib. On Apple Silicon the shell runs under Rosetta (x86_64),
# which would otherwise make RunUAT pick mac-x64 dotnet and build x86_64.
echo "== Packaging (Shipping) ==  (ARCH_PREFIX=$ARCH_PREFIX)"
$ARCH_PREFIX "$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
    -project="$PROJECT" \
    -noP4 -utf8output -unattended -nocompileeditor \
    -platform=Mac \
    -clientconfig=Shipping -serverconfig=Shipping \
    -build -cook -stage -pak -archive \
    -archivedirectory="$OUTDIR"

# --- Locate the staged .app bundle ------------------------------------------------
# RunUAT -archive copies only the executable wrapper into OUTDIR; the
# self-contained bundle (Contents/UE/<Project> with the .pak files) lives in
# Saved/StagedBuilds. Ship THAT, not the raw archive, or the game would launch
# with no content. The app is named <Project>-<Platform>-<Config>.app.
APP="$(find "$REPO_ROOT/Unreal/Born2Flap/Saved/StagedBuilds/Mac" -maxdepth 1 -name '*.app' -type d | head -1)"
if [ -z "$APP" ] || [ ! -d "$APP" ]; then
    echo "error: staged .app not found under Saved/StagedBuilds/Mac" >&2
    exit 1
fi
echo "APP=$APP"

# --- Stage the Haskell lib into the package --------------------------------------
# FPaths::ProjectDir() resolves to <app>/Contents/UE/<Project>/ in a packaged
# Mac build, so the bridge's ProjectDir()/Binaries/ThirdParty/<lib> maps here.
PKG_PROJECT_DIR="$APP/Contents/UE/Born2Flap"
mkdir -p "$PKG_PROJECT_DIR/Binaries/ThirdParty"
cp -f Unreal/Born2Flap/Binaries/ThirdParty/libborn2flap_math.dylib \
    "$PKG_PROJECT_DIR/Binaries/ThirdParty/"
echo "staged Haskell lib into $PKG_PROJECT_DIR/Binaries/ThirdParty/"

# --- Wrap into .dmg ---------------------------------------------------------------
DMG="$OUTDIR/BORN2FLAP-$TAG-mac.dmg"
hdiutil create -volname "BORN2FLAP" -srcfolder "$APP" -ov -format UDZO "$DMG"
echo "== Done: $DMG =="