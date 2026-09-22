#!/usr/bin/env bash
# Regenerate the paperdoll sheets + inventory icons straight into public/assets.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
export VALHALLA_OUT="$ROOT/public/assets/sprites/paperdoll"
export VALHALLA_ICON_OUT="$ROOT/public/assets/sprites/icons"
export VALHALLA_CELL="${VALHALLA_CELL:-64}"
mkdir -p "$VALHALLA_OUT" "$VALHALLA_ICON_OUT"
python3 render.py "$@"
python3 compose.py
echo "paperdoll assets written to $VALHALLA_OUT"
