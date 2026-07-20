#!/usr/bin/env bash
#
# apply_ppa_hang_fix.sh - Apply the ESP-IDF PPA "DIG-734" hang workaround.
#
# The esp_lvgl_adapter TRIPLE_PARTIAL tear-avoid mode with 90/270 rotation on the
# ESP32-P4 triggers a PPA (Pixel Processing Accelerator) hardware hang caused by
# the DIG-734 mb-order workaround in ESP-IDF's esp_driver_ppa/src/ppa_srm.c. The
# fix (published by esp_lvgl_adapter as
# 0001-bugfix-ppa-Temporary-fix-for-the-PPA-hang-issue.patch) replaces the whole
# DIG-734 block with an unconditional ppa_ll_srm_bypass_mb_order(dev, true).
#
# The upstream patch is a git context diff against ESP-IDF v6.0; the exact source
# of the block differs slightly between IDF releases (e.g. v5.5 computes the pixel
# depth differently), so applying it with `git apply`/`patch` fails on other
# versions. This script therefore performs the SAME semantic edit in a
# version-robust way: it collapses everything from the "Hardware bug workaround
# (DIG-734)" comment through the following ppa_ll_srm_bypass_mb_order(...) call
# into a single unconditional call. The committed upstream .patch is kept for
# provenance (see patches/README.md).
#
# Idempotent: does nothing once the DIG-734 block is gone. Invoked automatically
# from the top-level CMakeLists.txt for rotated ESP32-P4 boards, and runnable by
# hand:
#     source /opt/esp/idf/export.sh
#     patches/apply_ppa_hang_fix.sh
#
# It only ever touches components/esp_driver_ppa/src/ppa_srm.c under $IDF_PATH.
set -euo pipefail

: "${IDF_PATH:?IDF_PATH is not set; run 'source \$IDF_PATH/export.sh' first}"

TARGET="${IDF_PATH}/components/esp_driver_ppa/src/ppa_srm.c"

if [ ! -f "${TARGET}" ]; then
    echo "[ppa-hang-fix] ${TARGET} not found (esp_driver_ppa missing); skipping"
    exit 0
fi

# Already applied? The edit removes the "DIG-734" workaround comment.
if ! grep -q "Hardware bug workaround (DIG-734)" "${TARGET}"; then
    echo "[ppa-hang-fix] already applied to ${TARGET}"
    exit 0
fi

TMP="$(mktemp)"
trap 'rm -f "${TMP}"' EXIT

# Collapse the DIG-734 workaround block into a single unconditional call.
python3 - "${TARGET}" > "${TMP}" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
# Match from the DIG-734 comment up to and including the bypass_mb_order call,
# preserving the original indentation of the comment line.
pat = re.compile(
    r'([ \t]*)//[^\n]*Hardware bug workaround \(DIG-734\).*?'
    r'ppa_ll_srm_bypass_mb_order\([^;]*\);',
    re.DOTALL,
)
new, n = pat.subn(
    lambda m: m.group(1) + 'ppa_ll_srm_bypass_mb_order(platform->hal.dev, true);',
    src,
)
if n != 1:
    sys.stderr.write("expected exactly 1 DIG-734 block, found %d\n" % n)
    sys.exit(2)
sys.stdout.write(new)
PY

install_file() {
    # $IDF_PATH is writable as root (the espressif/idf CI image); fall back to
    # sudo for an unprivileged dev-container user.
    if cp "${TMP}" "${TARGET}" 2>/dev/null; then
        return 0
    elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null && \
         sudo cp "${TMP}" "${TARGET}" 2>/dev/null; then
        return 0
    fi
    return 1
}

if install_file; then
    echo "[ppa-hang-fix] applied DIG-734 PPA hang workaround to ${TARGET}"
else
    echo "[ppa-hang-fix] WARNING: could not write ${TARGET} (read-only IDF_PATH)." >&2
    echo "[ppa-hang-fix] Rotated boards may hang under TRIPLE_PARTIAL. Apply as root:" >&2
    echo "[ppa-hang-fix]   IDF_PATH=${IDF_PATH} patches/apply_ppa_hang_fix.sh" >&2
fi
