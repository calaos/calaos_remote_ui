# OTA-01: OTA lifetime — HAL accessor, singleton fix, TLS wiring

- **Priority:** P1
- **Effort:** M
- **Phase:** 3
- **Depends on:** CORE-03, AUTH-01, NET-01, CFG-01
- **Blocks:** SP-01, APP-01, OTA-02
- **Findings:** M6, C2 wiring — see ../AUDIT.md
- **Status:** backlog

## Objective
Fix the OTA manager's static-destruction-order fiasco, make `HalOta` a proper HAL-owned
singleton accessed via `HAL::getOta()` (eliminating the second independent instance), and
wire TLS verification into the OTA download.

## Files (exclusive ownership — do not edit anything else)
- `main/ota_manager.h` / `.cpp` (modify)
- `hal/hal_ota.h` (modify)
- `hal/hal.h` / `hal/hal.cpp` (modify — add accessor)
- `main/startup_page.cpp` (ONE call-site change at `:542` only — keep the diff minimal,
  SP-01 owns this file next phase)

## Context
`OtaManager::getInstance()` is a function-local static (`main/ota_manager.cpp:28-32`) that
subscribes to `AppStore` in `init()` and unsubscribes in its destructor (`:22-26`) — the
destructor runs at static-destruction time, after `AppStore` (also a singleton) may already
be destroyed: classic static-destruction-order fiasco (M6). `HalOta` bypasses the HAL
pattern: free factory `createOta()` (`hal/hal_ota.h:101`) is called independently at
`ota_manager.cpp:44` AND `startup_page.cpp:542`, yielding two instances with independent
state. OTA download must honor `verify_ssl`/`ca_cert` (CFG-01 + NET-01); its auth headers
already come from AUTH-01's shared builder. The reference-quality error handling in
`hal/esp32/esp32_ota.cpp` must not regress.

## Approach
1. `HAL::getOta()`: add a `HalOta` member to the HAL base (created lazily or at init by each
   backend, mirroring `getDisplay()/getNetwork()`); deprecate/remove the free `createOta()`
   (update `hal_ota.h`; both existing call sites move to the accessor).
2. OtaManager lifetime: owned explicitly instead of function-local static — either an
   intentionally-leaked Meyers singleton (`new`, never destroyed, documented) or ownership
   moved under `AppMain` with explicit `shutdown()` called before store teardown. Choose the
   smaller diff (leaky singleton recommended: destructor-at-exit ordering problems disappear;
   `init()`/`shutdown()` remain for deterministic unsubscribe). Ensure `unsubscribe` happens
   in `shutdown()`, not the destructor.
3. `startup_page.cpp:542`: replace the `createOta()` call with `HAL::getInstance().getOta()`.
   Touch nothing else in that file.
4. TLS: pass `verify_ssl`/`ca_cert` from device config into the OTA HTTP download path
   (`setTlsOptions` from NET-01). ESP32 note: if the ESP path uses `esp_https_ota` rather
   than the network/ HttpClient, wire the equivalent cert config there
   (`esp_http_client_config_t::cert_pem` / `crt_bundle_attach`) — inspect `esp32_ota.cpp`
   usage first (read-only; if a change is required there, it is in-scope as it implements
   `hal_ota.h`).
5. Keep `updateInProgress_` atomic semantics from CORE-03; document the auto-start-vs-progress
   race resolution in a comment if the logic needs a CAS.

## Out of scope
- OTA UI (`ota_update_screen.*` — OTA-02).
- StartupPage OTA-gating logic (SP-01/SP-02).
- Linux OTA real implementation (stub stays; HALL-01/DEP scope).

## Acceptance criteria
- [ ] Exactly one `HalOta` instance per process, reached via `HAL::getOta()`; `createOta()`
      free factory gone or private to the HAL backends.
- [ ] Clean process exit on Linux (run under ASAN/valgrind): no use-after-destruction from
      the OTA singleton path.
- [ ] OTA download honors verify_ssl (device with verify_ssl=true rejects a self-signed
      update server unless ca_cert pinned).
- [ ] ESP32 build passes; on-device OTA end-to-end flagged as manual follow-up [D].

## Verification
```bash
./build_linux.sh    # [L]
idf.py build        # [E]
# Smoke [S]: Linux simulator OTA simulation flow (check/download/progress UI).
# Device [D]: full OTA flash on a Waveshare panel — manual step, record in PR.
```
