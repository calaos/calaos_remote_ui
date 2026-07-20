#pragma once

// PERF-01 temporary benchmark harness.
//
// Compiled in only when PERF_BENCH is defined (e.g. `idf.py -DPERF_BENCH=1 ...`).
// When active, the app boots straight into a looping sequence of deterministic
// full-screen workloads instead of the normal UI, so the landscape-rotation
// pipeline (SW render -> PPA rotate -> DSI scanout) can be measured and A/B'd
// against a fixed load. Each scene runs for a few seconds and prints a
// "PERF01 SCENE" marker to serial; the per-frame flush/rotate/draw budget is
// emitted by the temporary instrumentation in esp_lvgl_port_disp.c.
//
// This file and its .cpp are throwaway diagnostic scaffolding — see
// docs/backlog/PERF-01-ppa-rotation-fps.md.

#ifdef PERF_BENCH

// Build the benchmark scene tree on the active screen and start the scene
// cycler + per-frame mutation timers. Must be called with the LVGL lock held
// (or from the LVGL task context).
void perfBenchStart();

#endif // PERF_BENCH
