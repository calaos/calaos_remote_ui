#!/usr/bin/env bash
#
# apply_dsi_clock_lane_fix.sh - Restore the pre-v5.5.5 MIPI-DSI clock lane
# sequencing in ESP-IDF.
#
# ESP-IDF commit f1f2babe06a ("feat(dsi): support to config clock lane mode",
# first released in v5.5.5) changed when the DSI clock lane leaves low-power
# mode:
#
#   before: esp_lcd_new_dsi_bus() left the clock lane in LP, and dpi_panel_init()
#           switched it to AUTO once the DPI stream was ready, so the panel's DCS
#           init sequence ran with the clock lane in LP.
#   after:  the bus comes up in AUTO (or HS with the new clock_lane_force_hs
#           flag) and the switch in dpi_panel_init() is gone, so the DCS init
#           sequence runs with the clock lane already active.
#
# The ST7703 panel on waveshare-86-panel does not survive that: the panel never
# displays the DPI stream and the screen stays white. Verified on hardware by
# writing a solid colour straight into the DPI frame buffer (bypassing LVGL): no
# output with stock v5.5.5, correct output once this edit is applied. Setting
# clock_lane_force_hs = 1 instead was also tested on hardware and does NOT fix
# it, so the new API offers no supported way out - hence this patch.
#
# The edit restores the v5.5.4 sequencing. Note it also neutralises the new
# clock_lane_force_hs flag (dpi_panel_init moves the lane to AUTO regardless);
# nothing in this project sets that flag.
#
# Version-robust and idempotent, like apply_ppa_hang_fix.sh next to it: it keys
# off the code shape rather than applying a context diff, and does nothing on an
# IDF that predates f1f2babe06a or that has already been fixed. The upstream
# commit is kept as 0002-feat-dsi-support-to-config-clock-lane-mode.patch for
# provenance (see patches/README.md).
#
# Invoked automatically from the top-level CMakeLists.txt, and runnable by hand:
#     source /opt/esp/idf/export.sh
#     patches/apply_dsi_clock_lane_fix.sh
#
# It only ever touches these two files under $IDF_PATH:
#     components/esp_lcd/dsi/esp_lcd_mipi_dsi_bus.c
#     components/esp_lcd/dsi/esp_lcd_panel_dpi.c
set -euo pipefail

: "${IDF_PATH:?IDF_PATH is not set; run 'source \$IDF_PATH/export.sh' first}"

BUS_C="${IDF_PATH}/components/esp_lcd/dsi/esp_lcd_mipi_dsi_bus.c"
DPI_C="${IDF_PATH}/components/esp_lcd/dsi/esp_lcd_panel_dpi.c"

for f in "${BUS_C}" "${DPI_C}"; do
    if [ ! -f "${f}" ]; then
        echo "[dsi-clock-lane] ${f} not found (no MIPI-DSI support); skipping"
        exit 0
    fi
done

# Already correct? Either the IDF predates f1f2babe06a, or we ran before.
if grep -q "MIPI_DSI_LL_CLOCK_LANE_STATE_LP" "${BUS_C}" && \
   grep -q "MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO" "${DPI_C}"; then
    echo "[dsi-clock-lane] already applied (or IDF predates f1f2babe06a)"
    exit 0
fi

install_file() {
    # $IDF_PATH is writable as root (the espressif/idf CI image); fall back to
    # sudo for an unprivileged dev-container user.
    local src="$1" dst="$2"
    if cp "${src}" "${dst}" 2>/dev/null; then
        return 0
    elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null && \
         sudo cp "${src}" "${dst}" 2>/dev/null; then
        return 0
    fi
    return 1
}

TMP_BUS="$(mktemp)"
TMP_DPI="$(mktemp)"
trap 'rm -f "${TMP_BUS}" "${TMP_DPI}"' EXIT

# 1. Bring the bus up with the clock lane in LP again.
python3 - "${BUS_C}" > "${TMP_BUS}" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
pat = re.compile(
    r'mipi_dsi_host_ll_set_clock_lane_state\(hal->host,[^;]*'
    r'MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO\);'
)
new, n = pat.subn(
    'mipi_dsi_host_ll_set_clock_lane_state(hal->host, MIPI_DSI_LL_CLOCK_LANE_STATE_LP);',
    src,
)
if n != 1:
    sys.stderr.write("expected exactly 1 clock lane state call in the bus, found %d\n" % n)
    sys.exit(2)
sys.stdout.write(new)
PY

# 2. Switch to AUTO once the DPI video stream is enabled, as v5.5.4 did.
python3 - "${DPI_C}" > "${TMP_DPI}" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
pat = re.compile(
    r'([ \t]*)(// enable the video mode\n[ \t]*mipi_dsi_host_ll_enable_video_mode\(hal->host, true\);\n)'
)
def repl(m):
    indent = m.group(1)
    return (m.group(0)
            + indent + '// switch the clock lane to high speed mode\n'
            + indent + 'mipi_dsi_host_ll_set_clock_lane_state(hal->host, MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO);\n')
hits = len(pat.findall(src))
if hits != 1:
    sys.stderr.write("expected the video-mode enable in dpi_panel_init, found %d\n" % hits)
    sys.exit(2)
sys.stdout.write(pat.sub(repl, src))
PY

if install_file "${TMP_BUS}" "${BUS_C}" && install_file "${TMP_DPI}" "${DPI_C}"; then
    echo "[dsi-clock-lane] restored pre-v5.5.5 DSI clock lane sequencing in ${IDF_PATH}"
else
    echo "[dsi-clock-lane] WARNING: could not write into ${IDF_PATH} (read-only)." >&2
    echo "[dsi-clock-lane] waveshare-86-panel will come up with a white screen. Apply as root:" >&2
    echo "[dsi-clock-lane]   IDF_PATH=${IDF_PATH} patches/apply_dsi_clock_lane_fix.sh" >&2
fi
