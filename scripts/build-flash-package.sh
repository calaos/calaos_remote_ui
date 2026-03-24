#!/usr/bin/env bash
# Build a flash package (.zip) for initial device flashing via esptool.
#
# This script collects all ESP32 binaries (bootloader, partition table, otadata,
# app) from the build directory, adds a config partition placeholder (0xFF),
# and packages everything with a manifest.json containing esptool parameters,
# offsets, and checksums.
#
# Usage: build-flash-package.sh <board-name> <version> <build-dir> <partition-csv>
#   board-name:     e.g. waveshare-86-panel
#   version:        e.g. 1.0.5 or 1.0.5-dev.3
#   build-dir:      path to the ESP-IDF build directory (contains flasher_args.json)
#   partition-csv:  path to the partition table CSV
#
# Outputs: pkg/calaos-remote-ui-<board>-<version>-flash.zip
#
# Requirements: jq, zip, python3 (for CRC generation)

set -euo pipefail

BOARD="${1:?Usage: build-flash-package.sh <board-name> <version> <build-dir> <partition-csv>}"
VERSION="${2:?Usage: build-flash-package.sh <board-name> <version> <build-dir> <partition-csv>}"
BUILD_DIR="${3:?Usage: build-flash-package.sh <board-name> <version> <build-dir> <partition-csv>}"
PARTITION_CSV="${4:?Usage: build-flash-package.sh <board-name> <version> <build-dir> <partition-csv>}"

FLASHER_ARGS="${BUILD_DIR}/flasher_args.json"

if [[ ! -f "${FLASHER_ARGS}" ]]; then
    echo "ERROR: flasher_args.json not found: ${FLASHER_ARGS}" >&2
    exit 1
fi

if [[ ! -f "${PARTITION_CSV}" ]]; then
    echo "ERROR: Partition CSV not found: ${PARTITION_CSV}" >&2
    exit 1
fi

ZIP_NAME="calaos-remote-ui-${BOARD}-${VERSION}-flash.zip"
STAGING_DIR=$(mktemp -d)
trap 'rm -rf "${STAGING_DIR}"' EXIT

echo "=== Building flash package for ${BOARD} v${VERSION} ==="
echo "  Build dir: ${BUILD_DIR}"
echo "  Partition CSV: ${PARTITION_CSV}"

# ── Extract esptool parameters from flasher_args.json ──────────────────────

CHIP=$(jq -r '.extra_esptool_args.chip' "${FLASHER_ARGS}")
BEFORE=$(jq -r '.extra_esptool_args.before' "${FLASHER_ARGS}")
AFTER=$(jq -r '.extra_esptool_args.after' "${FLASHER_ARGS}")
FLASH_MODE=$(jq -r '.flash_settings.flash_mode' "${FLASHER_ARGS}")
FLASH_SIZE=$(jq -r '.flash_settings.flash_size' "${FLASHER_ARGS}")
FLASH_FREQ=$(jq -r '.flash_settings.flash_freq' "${FLASHER_ARGS}")

echo "  Chip: ${CHIP}, Mode: ${FLASH_MODE}, Size: ${FLASH_SIZE}, Freq: ${FLASH_FREQ}"

# ── Collect flash binaries ─────────────────────────────────────────────────

# Build the binaries array from flash_files in flasher_args.json
# Each entry: offset -> relative path from build dir
BINARIES_JSON="[]"

for OFFSET in $(jq -r '.flash_files | keys[]' "${FLASHER_ARGS}"); do
    REL_PATH=$(jq -r --arg o "${OFFSET}" '.flash_files[$o]' "${FLASHER_ARGS}")
    SRC_FILE="${BUILD_DIR}/${REL_PATH}"

    if [[ ! -f "${SRC_FILE}" ]]; then
        echo "ERROR: Binary not found: ${SRC_FILE}" >&2
        exit 1
    fi

    # Flatten filename (bootloader/bootloader.bin -> bootloader.bin)
    FILENAME=$(basename "${REL_PATH}")

    # Handle potential name collisions by prefixing with directory name
    if [[ -f "${STAGING_DIR}/${FILENAME}" ]]; then
        DIR_PREFIX=$(dirname "${REL_PATH}" | tr '/' '-')
        FILENAME="${DIR_PREFIX}-${FILENAME}"
    fi

    cp "${SRC_FILE}" "${STAGING_DIR}/${FILENAME}"

    SHA256=$(sha256sum "${STAGING_DIR}/${FILENAME}" | awk '{print $1}')
    MD5=$(md5sum "${STAGING_DIR}/${FILENAME}" | awk '{print $1}')

    BINARIES_JSON=$(echo "${BINARIES_JSON}" | jq \
        --arg fn "${FILENAME}" \
        --arg off "${OFFSET}" \
        --arg sha "${SHA256}" \
        --arg md5 "${MD5}" \
        '. + [{"filename": $fn, "offset": $off, "checksum_sha256": $sha, "checksum_md5": $md5}]')

    echo "  Binary: ${FILENAME} @ ${OFFSET} (SHA256: ${SHA256:0:16}...)"
done

# ── Generate config partition placeholder ──────────────────────────────────

# Parse partition CSV to find the config partition (subtype 0x40)
CONFIG_LINE=$(grep -E '^\s*config\s*,' "${PARTITION_CSV}" || true)

if [[ -n "${CONFIG_LINE}" ]]; then
    # Extract offset and size from CSV: name, type, subtype, offset, size, flags
    CONFIG_OFFSET=$(echo "${CONFIG_LINE}" | awk -F',' '{gsub(/[[:space:]]/, "", $4); print $4}')
    CONFIG_SIZE_HEX=$(echo "${CONFIG_LINE}" | awk -F',' '{gsub(/[[:space:]]/, "", $5); print $5}')

    # Convert hex size to decimal for dd
    CONFIG_SIZE_DEC=$(printf '%d' "${CONFIG_SIZE_HEX}")

    echo "  Config partition: offset=${CONFIG_OFFSET}, size=${CONFIG_SIZE_HEX} (${CONFIG_SIZE_DEC} bytes)"

    # Generate placeholder filled with 0xFF
    python3 -c "import sys; sys.stdout.buffer.write(b'\\xff' * ${CONFIG_SIZE_DEC})" > "${STAGING_DIR}/config.bin"

    SHA256=$(sha256sum "${STAGING_DIR}/config.bin" | awk '{print $1}')
    MD5=$(md5sum "${STAGING_DIR}/config.bin" | awk '{print $1}')

    BINARIES_JSON=$(echo "${BINARIES_JSON}" | jq \
        --arg fn "config.bin" \
        --arg off "${CONFIG_OFFSET}" \
        --arg sha "${SHA256}" \
        --arg md5 "${MD5}" \
        '. + [{"filename": $fn, "offset": $off, "checksum_sha256": $sha, "checksum_md5": $md5}]')

    echo "  Binary: config.bin @ ${CONFIG_OFFSET} (placeholder ${CONFIG_SIZE_DEC} bytes)"
else
    echo "  Warning: No config partition found in ${PARTITION_CSV}, skipping config.bin"
fi

# ── Generate manifest.json ─────────────────────────────────────────────────

RELEASE_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

jq -n \
    --argjson schema 1 \
    --arg board "${BOARD}" \
    --arg version "${VERSION}" \
    --arg date "${RELEASE_DATE}" \
    --arg chip "${CHIP}" \
    --arg flash_mode "${FLASH_MODE}" \
    --arg flash_size "${FLASH_SIZE}" \
    --arg flash_freq "${FLASH_FREQ}" \
    --argjson baudrate 921600 \
    --arg before "${BEFORE}" \
    --arg after "${AFTER}" \
    --argjson binaries "${BINARIES_JSON}" \
    '{
        schema_version: $schema,
        board: $board,
        version: $version,
        release_date: $date,
        esptool: {
            chip: $chip,
            flash_mode: $flash_mode,
            flash_size: $flash_size,
            flash_freq: $flash_freq,
            baudrate: $baudrate,
            before: $before,
            after: $after
        },
        binaries: $binaries
    }' > "${STAGING_DIR}/manifest.json"

echo "  Manifest: ${STAGING_DIR}/manifest.json"

# ── Create zip archive ─────────────────────────────────────────────────────

mkdir -p pkg
(cd "${STAGING_DIR}" && zip -j "${OLDPWD}/pkg/${ZIP_NAME}" ./*)

echo "=== Flash package built: pkg/${ZIP_NAME} ==="
echo "  Contents:"
unzip -l "pkg/${ZIP_NAME}"
