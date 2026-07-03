# HALE-01: ESP32 network — graceful errors, WiFi reconnect, resource cleanup

- **Priority:** P1
- **Effort:** L
- **Phase:** 3
- **Depends on:** CORE-03
- **Blocks:** none
- **Findings:** M11, M12 (ESP32 part), M13 (network part) — see ../AUDIT.md
- **Status:** backlog

## Objective
Stop panicking the device on recoverable ethernet-init failures, handle WiFi loss after the
initial connection with automatic reconnect, and make every FreeRTOS resource created by the
network HAL deletable in `deinit()`.

## Files (exclusive ownership — do not edit anything else)
- `hal/esp32/esp32_hal_network.h` / `.cpp` (modify)

## Context
(M11) `initEthernet()` wraps 6 driver calls in `ESP_ERROR_CHECK`
(`hal/esp32/esp32_hal_network.cpp:219,225,233,243,246,250`) — any transient failure
(`esp_event_handler_register`, `esp_eth_start`…) panics/reboots the panel instead of
returning `HalResult::ERROR`. The graceful pattern already exists in the same file:
`initWifi()` checks every `esp_err_t` (`:288-360`) — mirror it.
(M12) After a successful connect, `WIFI_EVENT_STA_DISCONNECTED` only sets flags
(`:643-650`): no `esp_wifi_connect()` retry, no timeout restart — WiFi loss is permanent
until reboot (the WS layer's reconnect masks it only while IP remains valid).
(M13) `init()` creates a static `timeoutQueue`, `retrySemaphore_`, and `networkTimeoutTask`
running `while(true)` (`:182-196,:744-774`); `deinit()` deletes only the one-shot timer
(`:363-393`); the two `esp_event_handler_instance_register` handlers (`:296-316`) are never
unregistered (dangling `this` after teardown); `eth_handles` from `ethernet_init_all` leaks
(`:218-250`). An `ntp_sync` task is spawned per got-IP event (`:67,:700`) — verify it can't
pile up. All flags are already atomic (CORE-03, merged).

## Approach
1. Ethernet init: replace all 6 `ESP_ERROR_CHECK` with checked `esp_err_t` handling modeled
   on `initWifi` — log, clean up partial state (destroy netif/glue created so far, free
   `eth_handles`), return `HalResult::ERROR`. Network-init failure must leave the device
   running (UI can display the error state via the existing Flux events).
2. WiFi reconnect: on `WIFI_EVENT_STA_DISCONNECTED` after a previously successful
   connection, dispatch the network-lost event (Flux), then retry `esp_wifi_connect()` with
   bounded exponential backoff (reuse the existing retry timer machinery); on reconnect,
   normal got-IP flow resumes. Distinguish "never connected (initial timeout window)" from
   "connection lost" states explicitly.
3. Lifecycle: give the timeout task a shutdown command (queue message or event bit) so it
   exits its loop; `deinit()` joins/deletes task, queue, semaphore, unregisters both event
   handler instances, frees `eth_handles`. `init()` after `deinit()` must work (idempotent
   re-init).
4. Guard the per-got-IP `ntp_sync` task spawn against duplicates (skip if already running).
5. Keep `hal/esp32/esp32_hal.cpp` untouched (HALE-02 owns it); the deinit entry point is the
   existing `Esp32HalNetwork::deinit()`.

## Out of scope
- `esp32_hal.cpp`, `esp32_hal_system.cpp`, display/input (HALE-02).
- Linux backend (HALL-01).
- NTP retry logic internals beyond the duplicate-task guard (HALE-02 owns system/SNTP).

## Acceptance criteria
- [ ] No `ESP_ERROR_CHECK` remains in `esp32_hal_network.cpp` (grep); ethernet-init failure
      logs and returns ERROR without reboot.
- [ ] WiFi drop (AP off/on) → device reconnects automatically within the backoff schedule;
      Flux network events fire on loss and recovery. [D]
- [ ] `deinit()` deletes task/queue/semaphore/handlers/eth_handles; init→deinit→init cycle
      works. [D]
- [ ] `idf.py build` warning-free in this file (ratchet).

## Verification
```bash
idf.py build        # [E] — mandatory
./build_linux.sh    # [L] — must stay unaffected
# Device [D] — manual: boot with ethernet unplugged, with bad AP creds, AP power-cycle
# mid-session; record results in the PR.
```
