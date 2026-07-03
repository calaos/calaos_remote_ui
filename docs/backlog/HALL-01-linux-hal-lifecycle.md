# HALL-01: Linux HAL — WiFi-drop detection, lifecycle, real restart

- **Priority:** P1
- **Effort:** M
- **Phase:** 3
- **Depends on:** CORE-03, CORE-04
- **Blocks:** none
- **Findings:** M12 (Linux part), M13 (Linux part), m3 (checkWifiStatus), m5 — see ../AUDIT.md
- **Status:** backlog

## Objective
Detect network loss after initial connection on Linux, revive WiFi status reporting via the
argv ProcessRunner, fix deinit gaps (relay, detached thread), and make `restart()` actually
restart on the Luckfox target.

## Files (exclusive ownership — do not edit anything else)
- `hal/linux/linux_hal.cpp` (modify)
- `hal/linux/linux_hal_network.cpp` / `.h` (modify)
- `hal/linux/linux_hal_system.cpp` (modify)

## Context
(M12) `statusMonitorThread` only fires on `!network_connected_ && hasConnection`
(`hal/linux/linux_hal_network.cpp:476`) — the connected→lost transition is never detected,
so no `NetworkStatusChanged(false)` and no recovery flow. `checkWifiStatus()` is entirely
`#if 0` (`:418-463`); `getWifiStatus()` is meaningless on Linux (m3). (M13)
`LinuxHAL::deinit()` (`linux_hal.cpp:134-151`) never deinits relay — sysfs GPIOs stay
exported; `initNetworkAsync` uses a **detached** thread writing `networkReady_`
(`:106-123` — flag now atomic via CORE-03, but the thread is still unjoinable at deinit);
`getNetwork()` null-ref window (`:163`). (m5) `LinuxHalSystem::restart()` is `exit(0)` with
the real reboot commented out (`linux_hal_system.cpp:51-59`) — a no-op "restart" on the
actual Luckfox panel. CORE-04 (merged) provides `ProcessRunner` in this directory — use it
for every external command here; do not reintroduce `system()/popen()`.

## Approach
1. Status monitor: track previous state; on connected→lost emit `NetworkStatusChanged(false)`
   (same Flux event path as ESP32) and keep polling for recovery; on recovery emit
   reconnected. Poll interval: keep existing cadence.
2. Replace the `#if 0` `checkWifiStatus` with a `ProcessRunner`-based implementation
   (`nmcli -t -f ...` or `iw dev <if> link` — pick what the Luckfox image ships) so
   `getWifiStatus()` returns real states; if neither tool exists, return a documented
   UNSUPPORTED value instead of a fake DISCONNECTED.
3. Lifecycle: make the async network-init thread a joinable member joined in `deinit()`;
   add relay deinit to `LinuxHAL::deinit()` (unexport sysfs GPIOs — the relay HAL's own
   deinit from the BRD-01-generated table); `getNetwork()` gets the same guard/contract as
   HALE-02's ESP32 version (mirror the wording).
4. `restart()`: attempt real restart — `sync()` then `reboot(RB_AUTOBOOT)` when running as
   root (Luckfox), falling back to `exit(0)` for the desktop simulator (detect via
   geteuid/env); log which path was taken. Remove the commented-out cruft.

## Out of scope
- `process_runner.*` internals (CORE-04, merged).
- Linux OTA real implementation (`linux_ota.cpp` stub stays — see BOARD.md follow-ups).
- `hal/calaos_config/` (CFG-01, merged).

## Acceptance criteria
- [ ] Pulling the network (down the interface) on a running simulator emits the lost event;
      restoring emits reconnected (visible in logs/UI network status).
- [ ] `getWifiStatus()` returns real data on a machine with nmcli/iw; no `#if 0` remains.
- [ ] `deinit()` joins the init thread and unexports relay GPIOs (check
      /sys/class/gpio after exit on a target with relays).
- [ ] `restart()` reboots on target-like root environment, exits cleanly on desktop.

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: run simulator; `sudo ip link set <if> down/up`; observe events. Start/stop the
# app repeatedly; verify clean shutdown (no zombie threads — ps/ASAN).
```
