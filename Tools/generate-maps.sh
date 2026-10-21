#!/usr/bin/env bash
# Host Python only orchestrates; Unreal runs the generators with its own Python.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$REPO_ROOT/Tools/generate_maps.py" "$@"
