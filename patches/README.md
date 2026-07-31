# ESP-IDF patches

## `0001-bugfix-ppa-Temporary-fix-for-the-PPA-hang-issue.patch` (DIG-734)

Required for the rotated MIPI-DSI boards (waveshare-touchlcd-7/8/10) which drive
the display through `esp_lvgl_adapter` in `TRIPLE_PARTIAL` tear-avoid mode with a
90-degree rotation.

On the ESP32-P4 the PPA (Pixel Processing Accelerator) SRM path hits a hardware
hang (Espressif internal ticket **DIG-734**) when the adapter rotates partial
stripes directly into the DPI framebuffer. The workaround, published by
`espressif/esp_lvgl_adapter` as this exact patch, forces
`ppa_ll_srm_bypass_mb_order(dev, true)` unconditionally in
`components/esp_driver_ppa/src/ppa_srm.c`.

The `.patch` file is a verbatim copy of the one shipped inside the
`esp_lvgl_adapter` managed component (kept here because `managed_components/` is
git-ignored and regenerated). That upstream diff is a git context patch against
ESP-IDF **v6.0**; the exact lines of the DIG-734 block differ slightly between IDF
releases (v5.5 computes the pixel depth via `color_hal_pixel_format_get_bit_depth`,
v6.0 via the fourcc variant), so `git apply` / `patch` fails on other versions.
`apply_ppa_hang_fix.sh` therefore performs the **same semantic edit** in a
version-robust way: it collapses the whole `Hardware bug workaround (DIG-734)`
block down to a single `ppa_ll_srm_bypass_mb_order(platform->hal.dev, true);`,
which is exactly what the upstream patch produces. Verified against ESP-IDF
v5.5.5.

### How it is applied

It patches a file **inside `$IDF_PATH`** (the shared ESP-IDF install), so it is
applied by a documented, idempotent step rather than a silent hand-edit:

* Automatically: the top-level `CMakeLists.txt` runs `apply_ppa_hang_fix.sh`
  during CMake configure for ESP32 boards whose `BOARD_DISPLAY_ROTATION != 0`.
* Manually:

  ```bash
  source /opt/esp/idf/export.sh
  patches/apply_ppa_hang_fix.sh
  ```

The script only touches `components/esp_driver_ppa/src/ppa_srm.c` and is a no-op
if already applied. `$IDF_PATH` is writable when the build runs as root (the
`espressif/idf` CI image); on an unprivileged dev-container user the script falls
back to `sudo`.

### Verify

```bash
grep -n "ppa_ll_srm_bypass_mb_order" "$IDF_PATH/components/esp_driver_ppa/src/ppa_srm.c"
```

Patched: a single `ppa_ll_srm_bypass_mb_order(platform->hal.dev, true);` with the
`Hardware bug workaround (DIG-734)` block removed.

> Non-rotated boards (waveshare-86-panel) do not need this patch, but applying it
> is harmless.

---

## 2. MIPI-DSI clock lane sequencing (ESP-IDF ≥ v5.5.5)

`patches/apply_dsi_clock_lane_fix.sh` — provenance:
`0002-feat-dsi-support-to-config-clock-lane-mode.patch` (the upstream commit this
undoes).

### Why

ESP-IDF commit `f1f2babe06a` ("feat(dsi): support to config clock lane mode"),
first released in **v5.5.5**, changed when the DSI clock lane leaves low-power
mode:

| | ≤ v5.5.4 | ≥ v5.5.5 |
|---|---|---|
| `esp_lcd_new_dsi_bus()` | clock lane **LP** | clock lane **AUTO** (or HS via the new `clock_lane_force_hs` flag) |
| panel DCS init sequence | runs in **LP** | runs with the lane already active |
| `dpi_panel_init()` | switches to **AUTO** | switch removed |

The ST7703 panel on **waveshare-86-panel** does not survive the new sequencing:
the panel never displays the DPI stream and the screen stays white. The board
had been fine for months; it broke when `$IDF_PATH` was re-cloned and crossed
v5.5.4 → v5.5.5, which happened to be two days before an unrelated PPA/display
change — hence the initial misattribution.

Confirmed on hardware by writing a solid colour straight into the DPI frame
buffer, bypassing LVGL and the whole flush path: nothing on stock v5.5.5,
correct output with this patch applied. Setting `clock_lane_force_hs = 1`
instead was also tested on hardware and does **not** fix it, so the new API
offers no supported way out.

### Apply

Run automatically from the top-level `CMakeLists.txt` for every ESP32 board, or
by hand:

```bash
source /opt/esp/idf/export.sh
patches/apply_dsi_clock_lane_fix.sh
```

It only touches `components/esp_lcd/dsi/esp_lcd_mipi_dsi_bus.c` and
`components/esp_lcd/dsi/esp_lcd_panel_dpi.c`, is a no-op on an IDF that predates
`f1f2babe06a` or that is already patched, and falls back to `sudo` like the
DIG-734 script.

Note it also neutralises the new `clock_lane_force_hs` flag, since
`dpi_panel_init()` moves the lane to AUTO regardless. Nothing here sets it.

### Verify

```bash
grep -n "CLOCK_LANE_STATE" \
  "$IDF_PATH/components/esp_lcd/dsi/esp_lcd_mipi_dsi_bus.c" \
  "$IDF_PATH/components/esp_lcd/dsi/esp_lcd_panel_dpi.c"
```

Patched: `..._LP` in the bus, `..._AUTO` in `dpi_panel_init`.

> Unlike the DIG-734 patch this one is **not** gated on the board, because
> `$IDF_PATH` is shared between board builds — a per-board gate would only mean
> "whichever board configured last wins". The restored behaviour is what the
> touchlcd-7/8/10 boards shipped under historically, but they have not been
> re-checked on hardware since; worth a regression pass.
