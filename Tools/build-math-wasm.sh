#!/usr/bin/env bash
# Build the Haskell MathCore as a WebAssembly module for the iOS/Android ports.
#
# The wasm32-wasi-ghc backend (GHC 9.6.7) compiles the *same* Haskell sources
# into a single born2flap_math.wasm. The `foreign export ccall` declarations in
# MathCore/src/Born2Flap/Math/FFI.hs become WASM exports. The host (wasm3, a
# no-JIT interpreter — required on iOS) calls those exports directly.
#
# Prerequisites (one-time):
#   git clone https://gitlab.haskell.org/ghc/ghc-wasm-meta ~/ghc-wasm-meta
#   cd ~/ghc-wasm-meta && ./setup.sh      # installs wasi-sdk + wasm32-wasi-ghc
#
# Usage:
#   Tools/build-math-wasm.sh [--out=path]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT/MathCore"

OUT="$REPO_ROOT/MathCore/dist-newstyle/born2flap_math.wasm"
while [ $# -gt 0 ]; do
    case "$1" in
        --out=*) OUT="${1#*=}" ;;
        *) echo "usage: $0 [--out=path]" >&2; exit 2 ;;
    esac
    shift
done

export PATH="$HOME/.ghcup/bin:$PATH"

WASM_GHC="$(command -v wasm32-wasi-ghc || true)"
if [ -z "$WASM_GHC" ]; then
    echo "wasm32-wasi-ghc not found. Install the ghc-wasm toolchain first:" >&2
    echo "  git clone https://gitlab.haskell.org/ghc/ghc-wasm-meta ~/ghc-wasm-meta" >&2
    echo "  cd ~/ghc-wasm-meta && ./setup.sh" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"
echo "== Building MathCore WASM (wasm32-wasi) =="
# -no-hs-main: library, no `main`. -optl-mexec-model=reactor: WASI reactor,
# the RTS stays resident between exported calls (the simulation holds state
# behind StablePtr across Step() invocations).
wasm32-wasi-ghc -isrc -icbits -O2 -no-hs-main \
    -optl-mexec-model=reactor \
    src/Born2Flap/Math/FFI.hs cbits/bridge.c \
    -o "$OUT"

echo "== Done: $OUT =="
echo "== Verify exports (should list hs_b2f_math_* / b2f_math_*): =="
command -v wasm-objdump >/dev/null && wasm-objdump -x "$OUT" | grep -i b2f || true

# Emit a C header embedding the module bytes for the wasm3 adapter.
INC="${OUT%.wasm}_wasm.inc"
echo "== Embedding as $INC =="
xxd -i "$OUT" > "$INC"
echo "== (run a wasmtime smoke test before wiring into the app) =="
