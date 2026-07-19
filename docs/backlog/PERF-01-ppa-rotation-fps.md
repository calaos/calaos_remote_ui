# PERF-01: waveshare-touchlcd landscape rotation — diagnose & fix low FPS

- **Priority:** P1
- **Effort:** L
- **Phase:** 1
- **Depends on:** —
- **Blocks:** —
- **Boards:** waveshare-touchlcd-7 / -8 / -10 (ESP32-P4)
- **Status:** backlog

## Objective
Bring landscape rendering on the portrait-native touchlcd panels from <5-10 FPS to interactive
frame rates, by **measuring the real bottleneck first** and then applying targeted fixes. PPA
hardware rotation is already wired up — this is a performance-diagnosis task, not a "add PPA" task.

## Key finding (already true today)
PPA-accelerated 90° rotation is **already enabled**; the slowness is elsewhere:
- `sdkconfig.defaults.esp32p4:48` — `CONFIG_LVGL_PORT_ENABLE_PPA=y` (esp_lvgl_port rotates in its
  flush path via PPA when `sw_rotate=true`)
- `hal/esp32/esp32_hal_display.cpp:32,81` — `.sw_rotate = true`; `:109-121`
  `lv_display_set_rotation(LV_DISPLAY_ROTATION_90)`
- `sdkconfig.defaults.esp32p4:56-57` — `CONFIG_LV_USE_PPA=y` / `LV_USE_PPA_IMG=y` (LVGL PPA draw unit)
- IDF v5.5 (`driver/ppa.h`), LVGL 9.5 vendored + local PPA cache-sync patch, 128-B alignment set.

The actual flush/rotation loop lives in the **managed** `espressif/esp_lvgl_port` v2 (fetched at
build, not in the worktree), so root cause must be confirmed on hardware, not read statically.

## Ranked hypotheses (confirm by measurement)
1. **PSRAM bandwidth saturation** — LVGL draw buffers (`buff_spiram=true`,
   `esp32_hal_display.cpp:31`), PPA in/out, DPI framebuffers (`num_fbs` up to 3,
   `esp32_p4_wifi6_touch_lcd_x.c:1099,1137`) and **code** (`CONFIG_SPIRAM_XIP_FROM_PSRAM=y`,
   `sdkconfig.defaults:11`) all share the 200 MHz PSRAM bus; DSI scanout alone is ~110 MB/s.
2. **Whole-buffer msync per fill** — `components/lvgl/src/draw/espressif/ppa/lv_draw_ppa_fill.c:25`
   msyncs the entire `draw_buf->data_size` (~288 KB) per fill, not just the filled sub-rect.
3. **Single SW draw unit, no SIMD, no IRAM** — `main/lv_conf.h:171` `LV_DRAW_SW_DRAW_UNIT_CNT 1`,
   `:196` `LV_USE_DRAW_SW_ASM NONE`, `:466` `LV_ATTRIBUTE_FAST_MEM` empty (hot draw code runs from
   PSRAM XIP). 2nd P4 core idle for rendering.
4. **Blocking PPA dispatch** — `lv_draw_ppa.c:144-166` synchronous; no CPU/PPA overlap.
5. **PPA rotation branch may not actually run** in the fetched esp_lvgl_port (CPU fallback).

## Files
Diagnosis touches (temporarily) config + fetched sources; fixes land in:
- `hal/esp32/esp32_hal_display.cpp` — display/buffer/rotation config (only repo knob into the port)
- `main/lv_conf.h` — perf flags (sysmon, draw-unit count, ASM, fast_mem)
- `sdkconfig.defaults.esp32p4` / `sdkconfig.defaults` — PPA / PSRAM / DPI-buffer config
  (also fix the stray non-`CONFIG_`-prefixed `BSP_LCD_DPI_BUFFER_NUMS=2` no-op at `sdkconfig.defaults:22`)
- `components/lvgl/src/draw/espressif/ppa/lv_draw_ppa_fill.c` (+ `_buf.c`, `_img.c`) — msync scope
- `components/esp32_p4_wifi6_touch_lcd_x/esp32_p4_wifi6_touch_lcd_x.c:1092-1171` — DPI panel init
- (fetched, read-only) `managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c`

## Approach

### Phase 1 — Instrument & measure (devcontainer `espressif/idf:release-v5.5`, real hardware)
1. `idf.py -DBOARD=waveshare-touchlcd-7 reconfigure`, then read the fetched
   `managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c`: confirm the
   `sw_rotate` flush path calls `ppa_do_scale_rotate_mirror` (not a CPU loop) and whether partial
   dirty areas are expanded to full-width strips before rotating.
2. Enable LVGL perf monitor: `main/lv_conf.h:907` `LV_USE_SYSMON 1` (+ perf/mem monitor `:914`).
   Baseline FPS + CPU% on: idle screen, clock, a scroll/animation.
3. Bracket the flush callback and each PPA op with `esp_timer_get_time()`; log µs and ops/frame.
   Split the per-frame budget into render / rotate / `draw_bitmap` / scanout-stall.
4. Bandwidth A/B builds: (a) `BSP_LCD_DPI_BUFFER_NUMS=2`, (b) `SPIRAM_XIP_FROM_PSRAM` off (code from
   flash), (c) lower 7" DPI clock (`esp32_p4_wifi6_touch_lcd_x.c:1130`). Large FPS deltas = bandwidth.

**Deliverable:** a per-frame time budget naming the dominant cost.

### Phase 2 — Targeted fixes (apply only what Phase 1 justifies, EV order)
- **A.** msync sub-rect fix in `lv_draw_ppa_fill.c:25` (+ `_buf.c:44`, `_img.c:61,63`) — low risk.
- **B.** `LV_DRAW_SW_DRAW_UNIT_CNT 2` + `LV_ATTRIBUTE_FAST_MEM = IRAM_ATTR` — use both cores, stop
  XIP stalls in draw loops.
- **C.** Cut PSRAM contention: DPI FBs at 2, evaluate code-out-of-PSRAM if (b) helped, tune DPI clock.
- **D.** Rotation strategy: if partial areas expand to full strips, evaluate full-frame render + one
  full-frame PPA rotate/frame (trade render cost for one deterministic ~2-5 ms rotate).
- **E.** Non-blocking PPA (on_trans_done callback) to overlap PPA with CPU render.
- Minor: relax app-loop lock churn (`main/app_main.cpp:157-165`, 5 ms/200 Hz lock/unlock).

## Out of scope
- Non-rotated boards (`waveshare-86-panel`) — must keep building/rendering unchanged.
- Any UI/widget redesign; this is pipeline/config only.

## Acceptance criteria
- [ ] Phase-1 per-frame time budget documented (render vs rotate vs scanout), root cause identified.
- [ ] Landscape FPS clearly and stably improved (target: interactive, not <10) on 7"/8"/10.1".
- [ ] No rendering artifacts (the streaks the msync patch fixed must not reappear); touch stays
      aligned after rotation.
- [ ] `waveshare-86-panel` still builds and renders (regression check).

## Verification
```bash
# devcontainer, per board, on real hardware
idf.py -DBOARD=waveshare-touchlcd-7 build flash monitor   # read on-screen/serial FPS (sysmon)
idf.py -DBOARD=waveshare-touchlcd-8 build
idf.py -DBOARD=waveshare-touchlcd-10 build
idf.py -DBOARD=waveshare-86-panel build                   # regression
./build_linux.sh                                          # host build stays green
```
