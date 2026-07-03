# HALE-02: ESP32 lifecycle — real deinit, system fixes, getNetwork guard

- **Priority:** P1
- **Effort:** M
- **Phase:** 3
- **Depends on:** CORE-03, BRD-01
- **Blocks:** none
- **Findings:** M13 (system/display/input/relay parts), M11 (nvs site), m10 — see ../AUDIT.md
- **Status:** backlog

## Objective
Make the ESP32 HAL's deinit paths real and symmetric with init (relay, SNTP/semaphore,
display/input drivers), fix the remaining `ESP_ERROR_CHECK` panic site, and guard the
null-reference window in `getNetwork()`.

## Files (exclusive ownership — do not edit anything else)
- `hal/esp32/esp32_hal.h` / `.cpp` (modify)
- `hal/esp32/esp32_hal_system.h` / `.cpp` (modify)
- `hal/esp32/esp32_hal_display.cpp` (modify)
- `hal/esp32/esp32_hal_input.cpp` (modify)
- `hal/esp32/esp32_hal_relay.cpp` (modify — deinit hook only, table came from BRD-01)

## Context
(M13) `Esp32HAL::deinit()` (`hal/esp32/esp32_hal.cpp:146-164`) deinits network, input,
display, system — but never relay. `Esp32HalSystem::deinit()` is a stub
(`esp32_hal_system.cpp:58-62`): SNTP not stopped, `stopNtpRetryTimer()` not called,
`ntpSyncSemaphore` never deleted. `Esp32HalDisplay::deinit()` just nulls the pointer
(`esp32_hal_display.cpp:138-142` — no `lvgl_port`/panel teardown); `Esp32HalInput::deinit()`
likewise (`esp32_hal_input.cpp:40-44`). Duplicate dead `if (!display)` block at
`esp32_hal_display.cpp:95-105`. (M11) `esp32_hal_system.cpp:41` wraps `nvs_flash_erase()` in
`ESP_ERROR_CHECK` → panic on a recoverable condition. (M13/m10) `getNetwork()` returns
`*network` (`esp32_hal.cpp:176`) — null deref if called before async init populates it;
`init()` legacy path swallows network failure (`:29-31`, commented-out `return ERROR`) —
opposite of Linux semantics. Do NOT touch `esp32_hal_network.cpp` (HALE-01 owns it) — call
its existing/new `deinit()` only from `esp32_hal.cpp`.

## Approach
1. `Esp32HAL::deinit()`: add relay deinit (relay HAL gets a `deinit()` that de-asserts and
   releases GPIOs); keep teardown order inverse of init.
2. `Esp32HalSystem::deinit()`: stop SNTP (`esp_sntp_stop`), stop the NTP retry timer, delete
   `ntpSyncSemaphore`, then reset flags. Replace the `ESP_ERROR_CHECK(nvs_flash_erase())`
   with checked handling returning ERROR.
3. Display/input deinit: release what init acquired — `lvgl_port` teardown / panel handle
   deletion for display, touch driver release for input (follow the BSP APIs used at init;
   mind the documented BSP include-order trap at `esp32_hal_display.cpp:13-23` — read the
   comment, change nothing about include order). Remove the duplicated `if (!display)` block
   (`:95-105`).
4. `getNetwork()`: assert-and-log + late-init fallback (block until `isNetworkReady()` is
   not acceptable — prefer `abort-with-clear-log` in debug and a null-object/exception-free
   guard pattern consistent with the codebase; document the contract "call only after
   isNetworkReady()" in `hal/esp32/esp32_hal.h`).
5. Align `init()` failure semantics with Linux (restore `return HalResult::ERROR;` at
   `:29-31`) or delete the legacy `init()` if truly dead — check callers first
   (`app_main.cpp` `initFast` path; APP-01 deletes the app-level dead init, this is the
   HAL-level one — verify independently).

## Out of scope
- `esp32_hal_network.cpp` internals (HALE-01).
- Relay GPIO table (BRD-01, merged).
- `hal/hal.h`/`hal.cpp` (owned by OTA-01 this phase — the relay deinit is called from
  `esp32_hal.cpp`, no base-interface change needed; if one proves necessary, defer to APP-01
  phase and note it).

## Acceptance criteria
- [ ] Init→deinit→init cycle on device does not leak semaphores/timers and re-inits cleanly. [D]
- [ ] No `ESP_ERROR_CHECK` remains in owned files (grep).
- [ ] `getNetwork()` misuse produces a clear diagnostic instead of a null-reference crash.
- [ ] `idf.py build` passes warning-free in owned files.

## Verification
```bash
idf.py build        # [E] — mandatory
./build_linux.sh    # [L] — unaffected
# Device [D] — manual: reboot cycle test, OTA-triggered restart path exercises deinit.
```
