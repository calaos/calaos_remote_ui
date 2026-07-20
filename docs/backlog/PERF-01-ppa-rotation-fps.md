# PERF-01: waveshare-touchlcd landscape rotation — diagnose & fix low FPS

- **Priority:** P1
- **Effort:** L
- **Phase:** 1
- **Depends on:** —
- **Blocks:** —
- **Boards:** waveshare-touchlcd-7 / -8 / -10 (ESP32-P4)
- **Status:** implemented (7" measured; 8"/10"/86 build-verified, hardware-untested) — see Resolution

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

## Resolution (implemented)

### Measured root cause (Phase 1)
Instrumented the flush callback (esp_timer brackets: rotate / draw_bitmap / total) + an app-side
FPS counter, driven by a temporary on-boot benchmark harness (`main/perf_bench.cpp`, gated by
`-DPERF_BENCH=1`; four full-screen scenes: solid fill / rounded-rect SW draw / big clock / swipe).

- The flush path (PPA 90° rotate + framebuffer copy) dominates every scene (63–87% of wall time);
  SW render is secondary. Confirmed hypotheses **#1 (PSRAM bandwidth)** + **#4 (blocking, serialized
  rotate→scanout)**. **Rejected by A/B:** #5 (PPA path does run), rotation-strip expansion (rotates
  the exact dirty sub-rect), and **XIP-from-PSRAM off** (no change → XIP contention is NOT the cause).
- Effective flush throughput ~43–52 MB/s — the ESP32-P4 PSRAM wall for a strided 90° transpose
  (read row-major / write column-major destroys burst locality); matches Espressif's published
  P4 PSRAM→PSRAM memcpy (~51 MB/s). This is the FPS floor and it is **hardware physics**, not tuning.
- The `draw_bitmap` copy was a **blocking CPU PSRAM→PSRAM memcpy** (~5 ms/strip) because the DPI
  panel config never set `use_dma2d`.

### Fixes shipped
1. **`use_dma2d = true`** on the DPI panel configs (`components/esp32_p4_wifi6_touch_lcd_x/…c`, 7"
   ILI9881C + 8"/10" JD9365) → the framebuffer copy moves from a 5 ms blocking CPU memcpy to an
   async DMA2D 2D-copy (~0.05 ms).
2. **Custom tear-free rotating flush** (`hal/esp32/esp32_hal_display.cpp`, rotated boards only;
   the non-rotated 86-panel keeps `esp_lvgl_port`'s `lvgl_port_add_disp_dsi` unchanged). We keep
   esp_lvgl_port for the LVGL task/tick/lock/touch plumbing but replace only the display registration
   with our own `lv_display_create` + flush callback. It:
   - **H1** — PPA-rotates each dirty LVGL stripe **directly into the back DPI framebuffer**
     (`ppa_do_scale_rotate_mirror` with `.out.buffer` = the framebuffer, `.out.block_offset` = the
     rotated dest) — no intermediate PPA buffer, no second copy. Rotation coord math copied from the
     HW-verified `esp_lvgl_adapter` bridge.
   - **H8** — 2 DPI framebuffers + a **vsync-latched page flip** (draw_bitmap with a pointer inside
     the fb hits the DPI no-copy path; scanout switches at the frame boundary) → **tear-free**.
     `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2` (also fixed the stray non-`CONFIG_` no-op).
   - **Static correctness** — LVGL PARTIAL mode only flushes dirty sub-rects; before the first stripe
     of a frame we replay the **previous frame's dirty union** (front→back, `esp_async_fbcpy` 2D-DMA
     sub-rect), and **skip** the replay when that union ≥90% of the screen (full-screen animation
     overwrites everything anyway). This is what the esp_lvgl_adapter's TRIPLE_PARTIAL got wrong
     (black backgrounds on static UIs — see Rejected).
   - **H3** — non-blocking PPA (`PPA_TRANS_MODE_NON_BLOCKING` + `on_trans_done`), so LVGL renders the
     next stripe while the PPA rotates the current one. (The done-cb needs `oper.user_data = ctx`, or
     it faults — noted here because it is easy to miss.)
3. **DIG-734 PPA-hang workaround** (the custom flush uses PPA SRM): reproducible patch under
   `patches/`, auto-applied from `CMakeLists.txt` for rotated boards.
4. The vendored LVGL PPA draw unit (`CONFIG_LV_USE_PPA=y`) + its local msync streak-fix are **kept**
   (they work fine with a custom flush — the flush uses a separate PPA client).

### Tried and rejected after measurement
- **`espressif/esp_lvgl_adapter` (v0.6.2)** — evaluated as a ready-made alternative. `TRIPLE_PARTIAL`
  was fast (~37/32/15 FPS) but showed **black backgrounds on static UIs** (framebuffers never
  converge); `TRIPLE_FULL` rendered correctly but gave **no FPS gain** (~14/14/9.9, full-renders
  every update). Its own PPA draw unit also conflicted with our vendored one. Abandoned for the
  custom flush, which we control. (Also: the adapter/IDF6 rotate opposite chirality — 90↔270.)
- `LV_DRAW_SW_DRAW_UNIT_CNT 2` (2nd core): **no gain** — SW render is PSRAM-bandwidth-bound.
- **H2 (LVGL draw buffer in internal SRAM)** — *infeasible on this app*: only ~256 KB internal RAM,
  mostly consumed by WiFi/ESP-Hosted + LVGL by display-init time; 120–160 KB buffers don't fit, so it
  falls back to PSRAM. This is the main reason full-screen FPS didn't reach the ~25-35 estimate — the
  PPA input stays in PSRAM so the rotate stays PSRAM-bound (~43 ms/full frame).
- IDF 6 / LVGL upgrade: **neither lifts the FPS floor** (PPA non-blocking already in 5.5; the floor is
  PSRAM physics). Native landscape scanout: ILI9881C supports mirror only, **not** hardware swap_xy.

### Result (waveshare-touchlcd-7, hardware, bench FPS; all tear-free + correct)
| Scene | before (esp_lvgl_port) | + use_dma2d | **custom flush (final)** |
|-------|-----------------------|-------------|--------------------------|
| FILL (full-screen)  | ~11  | ~15.7 | ~14.4 |
| SWDRAW (SW draw)    | ~7   | ~7    | **~14.4** (2×) |
| CLOCK (partial)     | ~7.5 | ~9.5  | **~28.8** (3×) |
| SWIPE (full-screen) | ~8   | ~11.5 | **~13.4** |

Net: **tear-free**, with large gains on partial/moderate scenes (typical UI + the clock screensaver)
and a modest full-screen swipe gain. Static UI, rotation + touch verified correct on the 7".

### Residual / follow-ups
- **Full-screen swipe is at the PSRAM-transpose floor (~14 FPS).** The only remaining lever that
  changes the physics is a **portrait-native UI** (author the app in portrait, rotate only touch
  coords → deletes the transpose → est. ~35-50 FPS). Deferred as a product decision / separate ticket.
- The custom flush increments H2 (SRAM) / deeper H3 pipelining are diminishing returns here (H2 blocked
  by internal-RAM budget).
- 8"/10"/86-panel are build-verified only; need on-hardware validation (rotation sense, touch).
- The H2 SRAM attempt logs two boot warnings then falls back to PSRAM (cosmetic; left in for boards
  with more free internal RAM).
- `main/perf_bench.*` (the `-DPERF_BENCH` harness) is retained as a reusable perf tool, off by default.
