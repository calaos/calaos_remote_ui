# APP-01: AppMain lifecycle — teardown order, single init path, single ownership

- **Priority:** P1
- **Effort:** M
- **Phase:** 4
- **Depends on:** OTA-01, CORE-03
- **Blocks:** SP-02
- **Findings:** M5, m1 (g_wsManager), part of m3 (dead init) — see ../AUDIT.md
- **Status:** backlog

## Objective
Fix the shutdown use-after-free by destroying UI members before HAL teardown, collapse the
duplicated init paths, and consolidate the scattered globals (`g_appMain`, static `app`,
`g_wsManager`) into one ownership story.

## Files (exclusive ownership — do not edit anything else)
- `main/app_main.h` / `.cpp` (modify)
- `main/main.cpp` (modify)
- `main/calaos_websocket_manager.h` / `.cpp` (ONLY the `g_wsManager` global definition and
  its accessor replacement — coordinate: WSM-01 merged last phase, keep the diff surgical)
- `main/calaos_widget.cpp` (ONLY the `g_wsManager` consumer sites `:166-179`)
- `main/startup_page.cpp` (ONLY the `g_wsManager` assignment sites inside
  `connectWebSocketForVerification()` — SP-01 merged, the assignments now live in one method)

## Context
(M5) `~AppMain` calls `deinit()` → `hal->deinit()` FIRST (`main/app_main.cpp:33-37,:206-214`),
then member `unique_ptr`s (`otaScreen`, `screenSaver`, `notificationToast`, `stackView`)
destruct — their destructors call `lv_timer_del`/`lv_obj_del`/`AppStore::unsubscribe` against
torn-down LVGL. The Linux `run()` exit path manually resets only `screenSaver`+`stackView`
(`:183-184`), leaving `otaScreen`/`notificationToast` to die post-deinit. `init()` (legacy,
dead — `main.cpp:17` calls `initFast()`) duplicates the `smooth_ui_toolkit::ui_hal` lambda
setup (`:57-63` vs `:107-113`). Dual globals point at the same object: `g_appMain`
(`app_main.cpp:23`) used everywhere, file-static `app` (`main.cpp:12`) used by
`signalHandler`. (m1) `g_wsManager` (`calaos_websocket_manager.cpp:18`, `.h:10`) is a bare
mutable global owned de facto by StartupPage and dereferenced by widgets
(`calaos_widget.cpp:166-179`) — lifetime coupling through a global.

## Approach
1. Teardown order: explicit `shutdownUi()` that resets ALL UI-owning members
   (otaScreen, notificationToast, screenSaver, stackView — check for others) under the
   display lock, called from both the Linux `run()` exit path and `deinit()` BEFORE
   `hal->deinit()`. Destructor delegates to `deinit()` idempotently.
2. Delete legacy `init()`; rename/keep `initFast()` as the single `init()` (update
   `main.cpp` call). One `ui_hal` setup block remains.
3. Globals: keep exactly one access path to AppMain (`g_appMain` accessor or pass-through);
   `signalHandler` uses it; delete the redundant static.
4. `g_wsManager`: replace the bare global with an accessor pair owned by AppMain or a small
   registry (`CalaosWebSocketManager* activeWsManager()` / `setActiveWsManager(...)`) with
   null-safe semantics documented; widgets keep their existing null-check behavior via the
   accessor. This is a mechanical ownership cleanup, not a redesign — StartupPage still
   creates/owns the manager instance (SP-02 may revisit).
5. Verify clean-exit paths: SIGINT on Linux, window close, and the OTA-restart path
   (HAL::restart) all traverse the ordered shutdown.

## Out of scope
- StartupPage state machines (SP-02).
- HAL deinit internals (HALE-*/HALL-01, merged).
- Render-loop timing/`delay(5)` tuning (SWEEP-01 constants pass).

## Acceptance criteria
- [ ] Linux app exits ASAN/valgrind-clean via SIGINT and via window close (no
      use-after-free in LVGL teardown, no leaks from UI members).
- [ ] Exactly one init path; `grep -n 'initFast' main/` shows the collapse.
- [ ] `grep -n 'g_wsManager' main/` → only the accessor implementation (no bare extern
      global); widgets behave as before when no manager is active.
- [ ] ESP32 build passes; device reboot path still works.

## Verification
```bash
./build_linux.sh    # [L]
idf.py build        # [E]
# Smoke [S]: ASAN build; run full startup → main page; Ctrl-C → clean exit; repeat with
# window close; OTA-simulated restart.
```
