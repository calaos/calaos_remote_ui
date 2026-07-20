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
