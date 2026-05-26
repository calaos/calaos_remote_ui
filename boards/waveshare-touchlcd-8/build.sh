#!/usr/bin/env bash
# Build script for waveshare-touchlcd-8 (ESP32-P4 / ESP-IDF)
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

export BOARD
export APP_VERSION="${VERSION}"

idf.py -B build set-target esp32p4
idf.py -B build build

echo "=== Build complete: build/calaos-remote-ui.bin ==="

# Build flash package for initial flashing via esptool
chmod +x scripts/build-flash-package.sh
scripts/build-flash-package.sh \
    "${BOARD}" \
    "${VERSION}" \
    build \
    boards/waveshare-touchlcd-8/partitions.csv
