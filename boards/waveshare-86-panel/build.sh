#!/usr/bin/env bash
# Build script for waveshare-86-panel (ESP32-P4 / ESP-IDF)
#
# Called by CI with:
#   build.sh <board-name> <version>
#
# Expected environment: ESP-IDF toolchain available (source idf/export.sh)
# Output: build/calaos-remote-ui.bin

set -euo pipefail

BOARD="${1:?Usage: build.sh <board-name> <version>}"
VERSION="${2:?Usage: build.sh <board-name> <version>}"

echo "=== Building ${BOARD} firmware v${VERSION} (ESP-IDF) ==="

source /opt/esp/idf/export.sh

idf.py -B build set-target esp32p4
idf.py -B build build \
    -DBOARD="${BOARD}" \
    -DAPP_VERSION="${VERSION}"

echo "=== Build complete: build/calaos-remote-ui.bin ==="
