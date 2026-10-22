#!/usr/bin/env bash
# One-shot bootstrap for the ghc-wasm cross-toolchain inside a minimal Linux
# (WSL2 Ubuntu recommended). Installs base deps, clones ghc-wasm-meta, runs its
# setup.sh, then builds born2flap_math.wasm + the xxd embed for the app adapter.
#
# Run from inside the Linux guest after cloning/copying the repo:
#   Tools/setup-wasm-toolchain.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== [1/5] base packages =="
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update -y
    sudo apt-get install -y build-essential curl git xxd \
        libgmp-dev zlib1g-dev libtinfo-dev libncurses-dev \
        autoconf automake libtool cmake ninja-build pkg-config python3
elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y @development-tools curl git vim-common \
        gmp-devel zlib-devel ncurses-devel cmake ninja-build python3
else
    echo "Unsupported distro (need apt-get or dnf)." >&2
    exit 2
fi

echo "== [2/5] clone ghc-wasm-meta =="
if [ ! -d "$HOME/ghc-wasm-meta" ]; then
    git clone --depth 1 https://gitlab.haskell.org/ghc/ghc-wasm-meta "$HOME/ghc-wasm-meta"
fi

echo "== [3/5] ghc-wasm-meta/setup.sh (wasi-sdk + wasm32-wasi-ghc) =="
# Downloads bindists on first run; can take several minutes.
( cd "$HOME/ghc-wasm-meta" && ./setup.sh )

echo "== [4/5] build MathCore wasm =="
# build-math-wasm.sh sources ~/.ghc-wasm/env itself.
"$REPO_ROOT/Tools/build-math-wasm.sh"

echo "== [5/5] artifacts =="
ls -la "$REPO_ROOT/MathCore/dist-newstyle/" | grep -i -E 'wasm|inc' || true
echo "Done. Copy born2flap_math.wasm (+ _wasm.inc) back to the Windows host."
