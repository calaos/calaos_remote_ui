# CORE-03: Atomics for cross-thread connection/OTA/network flags

- **Priority:** P0
- **Effort:** S
- **Phase:** 1
- **Depends on:** none
- **Blocks:** AUTH-01, WSM-01, OTA-01, HALE-01, HALE-02, HALL-01, APP-01
- **Findings:** C3 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Mechanically convert every cross-thread state flag identified in C3 to `std::atomic`,
with no behavior change.

## Files (exclusive ownership — do not edit anything else)
- `main/calaos_websocket_manager.h` / `.cpp` (members + accessors only)
- `main/ota_manager.h` (member only)
- `hal/esp32/esp32_hal_network.h` / `.cpp`
- `hal/esp32/esp32_hal.h` / `.cpp`
- `hal/linux/linux_hal.h` / `.cpp`

## Context
These fields are written on one thread and read on others with no synchronization (C3):
- `currentState_`, `isConnecting_`, `consecutiveHandshakeErrors_`
  (`main/calaos_websocket_manager.h:161-163`) — written by net-thread callbacks
  (`:430,:457-458,:597`), read via `isConnected()` (`:246`) from the LVGL thread.
- `updateInProgress_` (`main/ota_manager.h:70`) — touched from dispatcher thread, OTA
  progress thread and callers (`ota_manager.cpp:86,114,143,163,216`).
- `networkConnected/ethernetConnected/wifiConnected/wifiStatus`
  (`hal/esp32/esp32_hal_network.h:47-61`) — event-loop task vs app threads, dual-core.
- `networkReady` (`hal/esp32/esp32_hal.h:38`) — async-init task vs main.
- `networkReady_` (`hal/linux/linux_hal.h:36`) — detached thread (`linux_hal.cpp:106-123`) vs main.

The codebase already does this correctly elsewhere — copy the style of
`Esp32HalSystem::ntpSynced` and Linux `network_connected_` (both `std::atomic`).

## Approach
1. Convert each listed member to `std::atomic<T>` (enum types included —
   `std::atomic<ConnectionState>` etc.). Default memory order (`seq_cst`) is fine at this
   call frequency; do not micro-optimize orderings.
2. Fix any compound read-modify-write revealed by the conversion (e.g.
   `consecutiveHandshakeErrors_++` → `fetch_add`; check-then-set sequences get a short
   comment noting the remaining logical race is addressed in WSM-01, not here).
3. No signature, logic, or log changes beyond what the type change forces.

## Out of scope
- Redesigning the reconnect/auth logic (WSM-01).
- `ProvisioningManager::config_` mutex (PROV-01).
- Mongoose-layer races (CORE-02).
- Adding new state or fixing deinit (HALE-*, HALL-01).

## Acceptance criteria
- [ ] All fields listed above are `std::atomic`; no plain reads/writes remain
      (grep the members).
- [ ] Behavior unchanged: connect/disconnect flow works in the simulator.
- [ ] Both platforms compile warning-free in the touched files (ratchet).

## Verification
```bash
./build_linux.sh     # [L]
idf.py build         # [E] — HAL headers changed
# Simulator smoke: start app, connect to server, kill server, watch reconnect.
```
