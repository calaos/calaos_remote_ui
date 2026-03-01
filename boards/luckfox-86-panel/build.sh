#!/usr/bin/env bash
# Build luckfox-86-panel firmware via Docker + buildroot
#
# Called by CI with:
#   build.sh <board-name> <version>
#
# Output: luckfox-pico-86-panel/build/output/luckfox_pico_86panel_w/images/update.img

set -euo pipefail

BOARD="${1:?Usage: build.sh <board-name> <version>}"
VERSION="${2:?Usage: build.sh <board-name> <version>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CALAOS_SRC="$(cd "$SCRIPT_DIR/../.." && pwd)"
LUCKFOX_DIR="$SCRIPT_DIR/luckfox-pico-86-panel"

echo "=== Building ${BOARD} firmware v${VERSION} (Buildroot + Docker) ==="

# Init submodule if needed
git -C "$CALAOS_SRC" submodule update --init --recursive -- "boards/luckfox-86-panel/luckfox-pico-86-panel"

make -C "$LUCKFOX_DIR" CONTAINER=1 CALAOS_SRC_PATH="$CALAOS_SRC" build

echo "=== Build complete: $LUCKFOX_DIR/build/output/luckfox_pico_86panel_w/images/update.img ==="
