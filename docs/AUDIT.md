# Technical-Debt Audit — calaos_remote_ui

> Source of truth for the refactoring backlog in `backlog/`. Every ticket references
> finding IDs from this document. Line anchors were verified on `main` at commit `438b576`
> (2026-07). Line numbers drift as tickets land — treat them as starting points, not gospel.

## Threading model (context for most findings)

Five concurrency contexts exist:

1. **LVGL/main thread** — `AppMain::run()` (`main/app_main.cpp:142`). Runs `renderLoop()`, all
   `LvglTimer` callbacks and all LVGL event callbacks.
2. **AppDispatcher worker** — Flux reducers and **every** `AppStore::subscribe` callback run
   here (StartupPage, CalaosPage, CalaosWidget, AboutPage, ScreenSaver, NotificationToast,
   OtaUpdateScreen, OtaManager) — *not* on the LVGL thread.
3. **WebSocket network task** — `onMessage/onStateChanged/onClose/onError`
   (`main/calaos_websocket_manager.cpp:380-625`) run on the net client thread.
4. **Discovery thread** — `CalaosDiscovery::discoveryThread` (`main/calaos_discovery.cpp:141`).
5. **Provisioning thread** — `ProvisioningRequester::requestThread` (`main/provisioning_requester.cpp:118`).

Cross-thread LVGL access is via `HAL::getDisplay().tryLock(100)` inside subscriber callbacks —
applied inconsistently, and a failed `tryLock` **silently drops the UI update**. The one correct
pattern (store snapshot in atomics on the dispatcher thread, apply in `update()` on the LVGL
thread) exists only in `OtaUpdateScreen`.

---

## Critical findings

### C1 — LvglTimer one-shots: untracked, unsynchronized, uncancelable → use-after-free
`main/lvgl_timer.cpp:204-233` (`createOneShot`) stores wrappers in a **file-static, unsynchronized
`std::vector`** (`:162`) and returns `nullptr` (`:232`), so callers cannot cancel a pending one-shot.
Lambdas capture `this` (StartupPage ~7 sites, ScreenSaver, WS auth-retry, widgets) → dangling
`this` if the owner is destroyed before the timer fires. The vector is mutated from the dispatcher
thread (`push_back` `:229`) and the LVGL thread (`lv_async_call` erase `:188-197`) concurrently.
Bonus: stub getters lie — `isPaused()` always `false` (`:144`), `getPeriod()`/`getRepeatCount()`
return `0` (`:151,158`).

### C2 — TLS certificate verification disabled on every secure channel
`network/websocket/websocket_client.cpp:490` and `network/http/http_client.cpp:396` hard-code
`opts.ca = nullptr`. `HttpClient` stores `default_verify_ssl_ = true` (`http_client.cpp:14,233`)
that is **never consulted** at the TLS-init site. Every consumer sets `verify_ssl = false`:
`main/calaos_websocket_manager.cpp:180` (with the only TODO in the codebase),
`main/provisioning_requester.cpp:176,400`, OTA download path. wss:// control channel and OTA
are MITM-able despite HMAC auth.
**Decided policy:** verify by default (embedded CA bundle) + per-server opt-out for self-signed
certs via provisioning config.

### C3 — Data races on cross-thread state flags (no atomics)
- `main/calaos_websocket_manager.h:161-163`: `currentState_`, `isConnecting_`,
  `consecutiveHandshakeErrors_` — written on net thread (`:430,:457-458,:597`), read from LVGL
  thread via `isConnected()` (`:246`, called from `calaos_widget.cpp:172`).
- `main/ota_manager.h:70`: `updateInProgress_` plain bool touched from 3 threads
  (`ota_manager.cpp:86,114,143,163,216`).
- `hal/esp32/esp32_hal_network.h:47-61`: `networkConnected/ethernetConnected/wifiConnected/wifiStatus`
  plain, written by event-loop task, read by app threads (dual-core ESP32-P4).
- `hal/esp32/esp32_hal.h:38` `networkReady` written by async-init task; `hal/linux/linux_hal.h:36`
  `networkReady_` written by a **detached** `std::thread` (`linux_hal.cpp:106-123`).
- Contrast (pattern to copy): `Esp32HalSystem::ntpSynced` and Linux `network_connected_` are
  correctly `std::atomic`.

### C4 — Shell command injection via provisioning config (Linux HAL)
`hal/linux/linux_hal_network.cpp` builds **17** `system()`/`popen()` command strings interpolating
device-config values: `nmcli dev wifi connect "<ssid>" password "<password>"` (`:156`),
`applyWifiConfig()` quote-wrapping (`:585,:636-648`), `applyStaticIpConfig()` interpolating
IP/gateway/iface into `ip addr`/`ip route` and overwriting `/etc/resolv.conf` (`:685-712`).
`hal/calaos_config/calaos_config.cpp` performs CRC/JSON validation but **no content validation**;
an SSID/DNS with shell metacharacters executes arbitrary commands (often as root on the panel).

### C5 — Mongoose driven from multiple threads
`network/websocket/websocket_client.cpp`: `mg_mgr_poll` runs on `serviceThread` (`:352-361`), but
`sendText/sendBinary/ping` call `mg_ws_send` from the caller thread via
`processOutgoingMessages()` (`:329-350,:259`) and `disconnect()` mutates `conn_->is_closing`
(`:187-191`). Mongoose requires all `mg_*` calls on the poll thread → data race / UAF on `conn_`.
Also: `scheduleReconnect()` reads/writes `current_config_`/`reconnect_attempts_` without
`config_mutex_` when invoked from the event handler (`:308-327,:471,:593`).

---

## Major findings

### M1 — StartupPage god object
`main/startup_page.cpp:391-1088`: `onStateChanged` is ~700 lines, holds the display lock
throughout, and interleaves 6 state machines (network, NTP, discovery, provisioning, WS-verify,
OTA gating) as flat `if` blocks over cached `lastXState` copies. Duplication: 3× "connect WS for
verification" (`:676-699`, `:759-778`, `:836-854`), 2× "start provisioning requester"
(`:709-722`, `:801-814`), 3× network-status string building (`:43-64`, `:455-465`, `:481-505`).
Dead: unused raw members `startup_page.h:34-38` (`spinner`, `statusLabel`, `provCodeLabel`,
`provCodeValue`, `provIpValue`); commented-out `testButtonCb` (`:1090-1116`).

### M2 — Store subscribers mutate LVGL from the dispatcher thread
All subscribers except `OtaUpdateScreen` reach into LVGL under best-effort `tryLock(100)` that
silently drops updates on contention. The correct snapshot/apply pattern
(`main/ota_update_screen.cpp`) should be generalized and all subscribers migrated.

### M3 — ProvisioningRequester stop is poll-and-detach
`main/provisioning_requester.cpp:66-111`: `stopRequesting` spins in 100 ms sleeps up to 2 s, then
**detaches**; a detached thread can later fire `onHttpResponse` into a destroyed owner.
Inconsistent with `CalaosDiscovery::stopDiscovery` which joins (`main/calaos_discovery.cpp:129`).
`verifyProvisioning` (`:381-389` area, ~120 lines) is dead code (verification moved to WS; no
callers). Minor: `j.dump(0)` pretty-print typo (`:324,:377`), unused `DEFAULT_SERVER_PORT` (`.h:92`),
`websocketUrl`/`httpApiUrl` extracted then unused (`:261-262`).

### M4 — ProvisioningManager lazy singleton race + unguarded config_
`main/provisioning_manager.cpp:286-295`: check-then-create on a file-static `unique_ptr`, called
from 4 threads. No mutex on `config_`: `resetProvisioning`/`completeProvisioning`/`saveConfig`
mutate it while `getAuthToken()`/`getDeviceSecret()` (`.h:76-79`) read it from the WS
auth-header builder. Dead: `generateDeviceInfoJson()` declared, never defined (`.h:86`).

### M5 — AppMain teardown order UAF + duplicated init paths
`main/app_main.cpp:33-37,206-214`: `~AppMain` runs `hal->deinit()` **before** member
`unique_ptr`s (`otaScreen`, `screenSaver`, `notificationToast`, `stackView`) destruct — their
destructors call `lv_*` against a torn-down display. The Linux `run()` exit path resets only
`screenSaver`+`stackView` (`:183-184`). `init()` vs `initFast()` duplicate the ui_hal setup
(`:57-63` vs `:107-113`); `init()` is dead (`main.cpp:17` calls `initFast()`). Dual globals:
`g_appMain` (`app_main.cpp:23`) + file-static `app` (`main.cpp:12`).

### M6 — OTA lifetime: static-destruction fiasco + double HalOta instance
`main/ota_manager.cpp:22-32`: function-local-static singleton subscribes to `AppStore`;
its destructor runs at exit after `AppStore` may be gone. `HalOta` uses a free factory
`createOta()` (`hal/hal_ota.h:101`) instead of a `HAL::getOta()` accessor and is instantiated
**twice** (`ota_manager.cpp:44` + `startup_page.cpp:542`) with independent state.

### M7 — StackView drops transitions; 4× duplicated animation setup
`main/stack_view.cpp:23-26,51-54`: `push`/`pop` silently no-op while `animating` (no log, no
queue) — a dropped push can strand the app on the startup screen.
`setupSlideVertical/HorizontalPush/Pop` (`:176-338`, ~160 lines) differ only in axis/sign.
Animation lambdas capture raw `PageBase*` (`:191,:232,:273,:314`); `clear()` (`:70`) ignores
in-flight animations.

### M8 — JSON parsing robustness (protocol + config updates)
- `main/calaos_protocol.cpp:68-106`: `std::stoi` on `x/y/w/h` inside
  `catch (const json::exception&)` (`:16,:160`) — `std::invalid_argument` **escapes**, is caught
  two frames up (`main/calaos_page.cpp:391`), which then **drops the entire page set**. One
  malformed widget coordinate blanks the whole UI.
- `main/calaos_websocket_manager.cpp:795,809,822`: `handleConfigUpdate` reads
  `data["brigtness"].get<int>()` (sic — server-side typo shim, default 80) and
  `std::stoi(screensaver_timeout/dimming)` unguarded; one malformed field discards the whole
  config update via the function-level catch (`:895`).
- `msgType = j["msg"]` unchecked type (`:394`).
- `PagesConfig` default-member-initializers call `HAL::getInstance()` (`main/calaos_protocol.h:88-89`)
  — hidden global side effect on every default construction, including the parse-failure return.

### M9 — Auth-error classification heuristic disables reconnect on benign failures
`main/calaos_websocket_manager.cpp`: `onClose` (`:453-540`) and `onError` (`:542-625`) hold two
near-identical errorType→httpCode blocks (`:479-497` vs `:559-576`). `isAuthenticationError`
(`:1146`) substring-matches `"invalid"`/`"token"`/`"auth"` anywhere in the reason string — a benign
disconnect is misclassified as fatal auth failure, disabling auto-reconnect (`:521`).
`consecutiveHandshakeErrors_ >= 3` (`:601`) permanently forces re-provisioning (`:606-617`) —
a flaky network is indistinguishable from bad credentials. Magic numbers in `connect()`:
30000/30000/5000/5 (`:177-183`).

### M10 — Discovery never propagates port/SSL; fake IP validation
`main/calaos_discovery.cpp:52-53,:252-253`: `CalaosServerFoundData` only sets `serverIp`;
`serverPort`/`serverSsl` stay defaulted (works by coincidence: default 5454/false).
IP "validation" is `find('.') != npos` (`:243`). Minor: `CALAOS_SERVER_IP` env back-door (`:30`),
`discovering_` stored outside the mutex in `onUdpDataReceived` (`:258`).

### M11 — ESP_ERROR_CHECK aborts the device on recoverable failures
`hal/esp32/esp32_hal_network.cpp:219,225,233,243,246,250` (ethernet init) +
`hal/esp32/esp32_hal_system.cpp:41` (`nvs_flash_erase`): transient failures panic/reboot instead
of returning `HalResult::ERROR`. Inconsistent with the graceful `initWifi` (`:288-360`) in the
same file.

### M12 — WiFi drop after initial connect is unhandled (both backends)
- ESP32: `WIFI_EVENT_STA_DISCONNECTED` sets flags but never restarts the timeout or calls
  `esp_wifi_connect()` (`hal/esp32/esp32_hal_network.cpp:643-650`).
- Linux: `statusMonitorThread` only detects disconnected→connected (`hal/linux/linux_hal_network.cpp:476`);
  connected→lost is never detected. `checkWifiStatus()` is entirely `#if 0` (`:418-463`).
The WS layer's own reconnect partially masks this.

### M13 — Init/deinit asymmetry, leaked FreeRTOS primitives
- Relay never deinitialized: `hal/esp32/esp32_hal.cpp:146-164`, `hal/linux/linux_hal.cpp:134-151`
  (sysfs GPIOs stay exported on Linux).
- `esp32_hal_network.cpp`: immortal `networkTimeoutTask` (`while(true)` `:744-774`), static
  `timeoutQueue` + `retrySemaphore_` (`:182-196`) never deleted; 2
  `esp_event_handler_instance_register` handlers never unregistered (`:296-316`); deinit deletes
  only the one-shot timer (`:363-393`); `eth_handles` allocation leaks (`:218-250`).
- `Esp32HalSystem::deinit()` is a stub (`esp32_hal_system.cpp:58-62`): SNTP not stopped, retry
  timer not stopped, `ntpSyncSemaphore` leaks.
- `Esp32HalDisplay::deinit()` just nulls the pointer (`esp32_hal_display.cpp:138-142`);
  `Esp32HalInput::deinit()` likewise (`esp32_hal_input.cpp:40-44`).
- `getNetwork()` returns a null reference during async init: `esp32_hal.cpp:176`,
  `linux_hal.cpp:163` — no guard.

### M14 — Widget copy-paste across 6 widgets
`main/widgets/`: `LightSwitchWidget`, `LightSwitchWideWidget`, `ScenarioWidget`, `ShutterWidget`,
`TemperatureWidget`, `ClockWidget` each repeat: container styling block
(`light_switch_widget.cpp:36-40`, `light_switch_wide_widget.cpp:47-51`, `scenario_widget.cpp:41-45`,
`shutter_widget.cpp:53-57`, `temperature_widget.cpp:31-34`, `clock_widget.cpp:116-120`);
the `lv_event_get_user_data` click trampoline (every widget); the ON/OFF `updateVisualState`
swap; `parseIsOn` identical in `light_switch_widget.cpp:160-179` and
`light_switch_wide_widget.cpp:165-180`; the hand-rolled `updatingFromServer` reentrancy flag.
Inconsistent numeric parsing rigor: guarded `stoi` (light switches) vs `strtod`+endptr
(`temperature_widget.cpp:83-91`) vs bare `atoi`/`strtol` (`shutter_common.h:41,47`).

### M15 — Cross-module duplication: auth headers, fonts/date, TZ fight
- `buildAuthHeaders` duplicated: `main/ota_manager.cpp:229-255` vs
  `main/calaos_websocket_manager.cpp:349-378` — identical HMAC construction, both log secrets.
- Font-selection machinery (`FontEntry`+`selectFont`+font tables) and `formatDate` duplicated:
  `main/screensaver.cpp:12-37,166-182,379` vs `main/widgets/clock_widget.cpp:12-41,94-111,191`.
- **Global TZ fight**: `screensaver.cpp:356-357` and `clock_widget.cpp:212-213` both call
  `setenv("TZ", ...); tzset();` every second with potentially different zones — libc global state
  corrupted between modules.
- `ScreenSaver::applyConfig` (dispatcher thread) calls `setBacklight` outside the display lock
  (`:339,:345`) and mutates `clock*_` strings racing the LVGL-thread readers (`:307-315`).

### M16 — Board/build duplication; relay hard-capped at 2
- The 4 Waveshare board Dockerfiles are **byte-identical**; `build.sh`/`.cmake` near-identical
  (differ in name/resolution/paths only). A new board = ~6 copied files + a redundant CI image.
- `cmake/board_config.h.in:40-41` emits exactly `BOARD_RELAY_1_GPIO`/`BOARD_RELAY_2_GPIO`; the
  array `{BOARD_RELAY_1_GPIO, BOARD_RELAY_2_GPIO}` is re-declared in
  `hal/esp32/esp32_hal_relay.cpp:14,52` and `hal/linux/linux_hal_relay.cpp:15,60,89` (5 sites);
  `BOARD_RELAY_COUNT > 2` silently reads out of bounds.
- `boards/luckfox-86-panel.cmake:24-25`: placeholder `GPIO 0 # TODO` — GPIO 0 is a real pin.
- Fragile BSP symbol-collision workaround via `EXCLUDE_COMPONENTS` + include-order hack
  (`esp32_hal_display.cpp:13-23`).

### M17 — Build flags: no warnings, Release silently ignored
No `-Wall/-Wextra` anywhere (`CMakeLists.txt:160` sets only `-g`). `build_linux.sh:29` passes
`-DCMAKE_BUILD_TYPE=Release` but `CMakeLists.txt:159` hard-codes `Debug`, overriding it — Linux
is always Debug. Dead `$? -eq 0` check under `set -e` (`build_linux.sh:36`). Minor CMake cruft:
`find_package(Python3)` twice (`:102,:152`); dead `LV_USE_LINUX_FBDEV/EVDEV` vars (`:166-172`).

### M18 — No tests, no PR CI, board-matrix drift
No CTest/unit targets anywhere. Both workflows (`.github/workflows/build-firmware.yml`,
`build-docker-images.yml`) trigger only on push to `main`/manual — nothing validates PRs.
`boards/ci-boards.json` lists 5 boards but the `workflow_dispatch` dropdown offers 2
(`build-firmware.yml:10-14`); the luckfox matrix leg has no `bin_path` yet upload uses
`if-no-files-found: error` (`:90-95`) → guaranteed failure.

### M19 — Secrets logged
Auth token, nonce, HMAC at DEBUG (`main/calaos_websocket_manager.cpp:364-367`); full
provisioning response incl. `auth_token`/`device_secret` at INFO
(`main/provisioning_requester.cpp:233,241`); raw JSON payload incl. plaintext `wifi_password`
(`hal/calaos_config/calaos_config.cpp:105`).

---

## Minor findings (bundled into related tickets)

- **m1** `g_wsManager` mutable global raw pointer (`calaos_websocket_manager.cpp:18`, `.h:10`),
  owned by StartupPage, dereferenced by widgets (`calaos_widget.cpp:166-179`). → APP-01
- **m2** OTA screen stringly-typed commands `"show:"/"error:"/"idle"` parsed with `rfind`
  (`ota_update_screen.cpp:244,274,309-330`); split atomic/mutex state can be observed
  inconsistently. → OTA-02
- **m3** Dead code: `verifyProvisioning`, `hexToBytes` (+unguarded `std::stoi`,
  `hmac_authenticator.cpp:99-106`), `generateDeviceInfoJson`, `AppMain::init()`, `testButtonCb`,
  `checkWifiStatus` `#if 0`, LvglTimer lying stub getters. → PROV-01, SWEEP-01, APP-01, HALL-01
- **m4** `ERROR0/1/2` magic-string error signalling from crypto
  (`provisioning_manager.cpp:272`, `provisioning_crypto.cpp:156,170`) checked via
  `starts_with("ERROR")` (`provisioning_manager.cpp:138`). → SWEEP-01
- **m5** `LinuxHalSystem::restart()` is `exit(0)` (`linux_hal_system.cpp:51-59`); Linux OTA is a
  simulation stub (`linux_ota.cpp:52-55,178-186`). → HALL-01
- **m6** Magic numbers pervasive: WS timeouts, layout offsets (`-720/-180/500x220`), timer delays
  (10/100/800/3000 ms), DPI 180, buffer `HRes*200`, task stacks (all priority 5). → owning tickets + SWEEP-01
- **m7** Comment cruft `// CHANGED:`/`// NEW:` (`calaos_page.h:21-39`); theme greys inlined at 5+
  sites instead of `theme.cpp`. → SWEEP-01
- **m8** `CalaosWidget::getDisplayName()` returns refs into members/params
  (`calaos_widget.cpp:58-74`); `onAppStateChanged` change-detection compares only 4 of 7 fields
  (`:125-128`). → WID-01
- **m9** `ImageSequenceAnimator` advertises `config_.threadSafe` but provides no locking
  (`image_sequence_animator.cpp:107-135`); ~200 lines of unused speculative API. → SWEEP-01
- **m10** ESP32 `HAL::init()` swallows network-init failure (`esp32_hal.cpp:29-31`,
  commented-out `return ERROR`) while Linux returns ERROR (`linux_hal.cpp:29`) — opposite
  semantics. Member-naming drift (`network` vs `network_`). → HALE-02 / SWEEP-01
- **m11** Vendored deps: LVGL 9.5.0 (5418 files, locally patched — see
  `sdkconfig.defaults.esp32p4:50-56`), nlohmann-json (1152 files incl. tests/docs),
  mongoose 7.8; `smooth_ui_toolkit` has two entry points (top-level + `components/` wrapper). → DEP-01
- **m12** Flux minors: identical `#ifdef` branches (`flux/app_dispatcher.cpp:4-8`); busy-poll
  teardown (`:186-198`); `AppState::operator==` deliberately skips `ioStates`/`config`
  (`flux/app_store.h:217`, documented perf choice). → SWEEP-01 (doc only)

## Explicitly healthy (do not "fix")

- Flux store/dispatcher design: subscriber copy-under-lock then lock-free invoke
  (`flux/app_dispatcher.cpp:112-132`); real `subscribe/unsubscribe(SubscriptionId)` — no dangling
  callbacks at store level.
- `hal/esp32/esp32_ota.cpp`: every `esp_err_t` checked, full cleanup on error paths — reference
  quality for HALE tickets.
- Zero platform `#ifdef` leakage into `main/`/`flux/` — preserve this property in every ticket.
- Board validation in root CMake (platform/board match, color-depth range checks).
