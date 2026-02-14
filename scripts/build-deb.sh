#!/usr/bin/env bash
# Build a Debian package containing the firmware binary + manifest for a board.
#
# Usage: build-deb.sh <board-name> <version> <bin-path>
#   board-name: e.g. waveshare-86-panel
#   version:    e.g. 1.0.5 or 1.0.5-dev.3
#   bin-path:   path to the firmware .bin file
#
# Outputs: pkg/calaos-remote-ui-<board>_<version>_all.deb

set -euo pipefail

BOARD="${1:?Usage: build-deb.sh <board-name> <version> <bin-path>}"
VERSION="${2:?Usage: build-deb.sh <board-name> <version> <bin-path>}"
BIN_PATH="${3:?Usage: build-deb.sh <board-name> <version> <bin-path>}"

if [[ ! -f "${BIN_PATH}" ]]; then
    echo "ERROR: Binary file not found: ${BIN_PATH}" >&2
    exit 1
fi

PKG_NAME="calaos-remote-ui-${BOARD}"
# dpkg uses ~ for pre-release sorting (1.0.0~dev.1 sorts before 1.0.0)
DEB_VERSION="${VERSION//-dev./~dev.}"
PKG_DIR="pkg/${PKG_NAME}_${DEB_VERSION}_all"
INSTALL_DIR="${PKG_DIR}/usr/share/calaos/remote-ui/firmwares/${BOARD}"

echo "=== Building .deb for ${BOARD} v${VERSION} ==="
echo "  Binary: ${BIN_PATH}"
echo "  Package: ${PKG_NAME}"

# Clean and create directory structure
rm -rf "${PKG_DIR}"
mkdir -p "${INSTALL_DIR}"
mkdir -p "${PKG_DIR}/DEBIAN"

# Copy the firmware binary
cp "${BIN_PATH}" "${INSTALL_DIR}/calaos-remote-ui.bin"

# Compute SHA256
CHECKSUM=$(sha256sum "${INSTALL_DIR}/calaos-remote-ui.bin" | awk '{print $1}')
RELEASE_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# Generate manifest.json
cat > "${INSTALL_DIR}/manifest.json" <<EOF
{
    "schema_version": 1,
    "hardware_id": "${BOARD}",
    "version": "${VERSION}",
    "firmware_file": "calaos-remote-ui.bin",
    "checksum_sha256": "${CHECKSUM}",
    "name": "Calaos Remote UI",
    "release_date": "${RELEASE_DATE}",
    "description": "${BOARD} firmware"
}
EOF

# Generate DEBIAN/control
cat > "${PKG_DIR}/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${DEB_VERSION}
Architecture: all
Depends: curl
Maintainer: Calaos Team <team@calaos.fr>
Description: Calaos Remote UI firmware for ${BOARD}
 This package contains the firmware binary and manifest
 for the ${BOARD} board. It is deployed to calaos-os
 based Debian systems for OTA updates.
Section: misc
Priority: optional
EOF

# Generate DEBIAN/postinst – notify calaos service to rescan firmwares
cat > "${PKG_DIR}/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e

RESCAN_URL="http://127.0.0.1:5454/api/v3/ota/rescan"

if ! curl --silent --show-error --max-time 5 -X POST "${RESCAN_URL}" > /dev/null 2>&1; then
    echo "Warning: could not notify calaos service for firmware rescan (is it running?)." >&2
    echo "A rescan will happen on next service restart." >&2
fi

exit 0
POSTINST
chmod 0755 "${PKG_DIR}/DEBIAN/postinst"

# Generate DEBIAN/postrm – notify calaos service after firmware removal
cat > "${PKG_DIR}/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e

if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    RESCAN_URL="http://127.0.0.1:5454/api/v3/ota/rescan"

    if ! curl --silent --show-error --max-time 5 -X POST "${RESCAN_URL}" > /dev/null 2>&1; then
        echo "Warning: could not notify calaos service for firmware rescan (is it running?)." >&2
        echo "A rescan will happen on next service restart." >&2
    fi
fi

exit 0
POSTRM
chmod 0755 "${PKG_DIR}/DEBIAN/postrm"

# Build the .deb
dpkg-deb --root-owner-group --build "${PKG_DIR}"

# Move .deb to pkg/ root
DEB_FILE="${PKG_DIR}.deb"
echo "=== Package built: ${DEB_FILE} ==="
echo "  Contents:"
dpkg-deb -c "${DEB_FILE}"
