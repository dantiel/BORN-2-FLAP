#!/usr/bin/env bash
# Build the static Android physics backend: wasm3 (no-JIT interpreter) +
# the b2f_math_* adapter (Native/src/born2flap_math_wasm.cpp) into
# Native/lib/libborn2flap_math_wasm.a for arm64-v8a.
#
# Born2Flap.Build.cs links this archive on UnrealTargetPlatform.Android, so the
# Android target resolves the ::b2f_math_* symbols that Born2FlapMathBridge.cpp
# calls directly (dlopen is forbidden on Android).
#
# Prerequisites (one-time):
#   1. Build the wasm module first:  Tools/build-math-wasm.sh
#      -> produces MathCore/dist-newstyle/born2flap_math.wasm
#      -> and       MathCore/dist-newstyle/born2flap_math_wasm.inc (embedded bytes)
#   2. Vendor wasm3:
#      git submodule add https://github.com/wasm3/wasm3.git Native/ThirdParty/wasm3
#
# Usage:
#   Tools/build-wasm3-android.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK="${ANDROID_NDK_HOME:-V:/SHARED/AndroidSDK/ndk/27.2.12479018}"
API="${ANDROID_API_LEVEL:-26}"
WASM3="$REPO_ROOT/Native/ThirdParty/wasm3"
INC="$REPO_ROOT/MathCore/dist-newstyle/born2flap_math_wasm.inc"
OUT_DIR="$REPO_ROOT/Native/lib"
OUT="$OUT_DIR/libborn2flap_math_wasm.a"

if [ ! -f "$INC" ]; then
    echo "Missing embedded wasm bytes: $INC" >&2
    echo "Run Tools/build-math-wasm.sh first (needs the ghc-wasm toolchain)." >&2
    exit 1
fi

if [ ! -d "$WASM3" ]; then
    echo "wasm3 not vendored at $WASM3" >&2
    echo "  git submodule add https://github.com/wasm3/wasm3.git Native/ThirdParty/wasm3" >&2
    exit 1
fi

CLANG="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android${API}-clang"
AR="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-ar"
if [ ! -x "$CLANG" ]; then
    echo "NDK clang not found: $CLANG" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# wasm3 core + the WASI shim the adapter links (m3_LinkWASI). No JIT; the
# d_m3MaxFunctionStack is left at wasm3's default (raised at runtime via the
# adapter's m3_NewRuntime memory budget).
WASM3_SRCS=(
    "$WASM3/source/m3_api_libc.c"
    "$WASM3/source/m3_api_meta_wasi.c"
    "$WASM3/source/m3_api_wasi.c"
    "$WASM3/source/m3_bind.c"
    "$WASM3/source/m3_code.c"
    "$WASM3/source/m3_compile.c"
    "$WASM3/source/m3_core.c"
    "$WASM3/source/m3_env.c"
    "$WASM3/source/m3_exec.c"
    "$WASM3/source/m3_function.c"
    "$WASM3/source/m3_info.c"
    "$WASM3/source/m3_module.c"
    "$WASM3/source/m3_parse.c"
)

OBJS=()
for src in "${WASM3_SRCS[@]}"; do
    [ -f "$src" ] || { echo "Missing wasm3 source: $src" >&2; exit 1; }
    obj="$OUT_DIR/$(basename "${src%.c}").o"
    "$CLANG" -O2 -fno-strict-aliasing -fno-omit-frame-pointer \
        -Dd_m3VerboseErrorMessages=1 \
        -I"$WASM3/source" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

# Adapter: born2flap_math_wasm.cpp (guarded by B2F_WASM_BACKEND_ENABLED) with the
# embedded wasm bytes, compiled as C++ against wasm3 headers.
"$CLANG" -O2 -fno-strict-aliasing -fno-exceptions -fno-rtti \
    -DB2F_WASM_BACKEND_ENABLED=1 \
    -I"$WASM3/source" -I"$REPO_ROOT/Native/include" \
    -include "$INC" \
    -c "$REPO_ROOT/Native/src/born2flap_math_wasm.cpp" \
    -o "$OUT_DIR/born2flap_math_wasm.o"
OBJS+=("$OUT_DIR/born2flap_math_wasm.o")

rm -f "$OUT"
"$AR" rcs "$OUT" "${OBJS[@]}"

echo "== Done: $OUT =="
echo "== Objects: ${OBJS[*]} =="
