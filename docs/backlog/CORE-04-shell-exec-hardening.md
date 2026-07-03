# CORE-04: Eliminate shell injection in the Linux network HAL

- **Priority:** P0
- **Effort:** M
- **Phase:** 1
- **Depends on:** none
- **Blocks:** HALL-01
- **Findings:** C4 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Replace every `system()`/`popen()` shell-string call in the Linux network HAL with an
argv-based process runner so that provisioning-config values (SSID, password, IP, DNS) can
never be interpreted by a shell.

## Files (exclusive ownership — do not edit anything else)
- `hal/linux/linux_hal_network.cpp` / `.h` (modify)
- `hal/linux/process_runner.h` / `.cpp` (new)

## Context
`hal/linux/linux_hal_network.cpp` builds **17** shell command strings interpolating
device-config values: `nmcli dev wifi connect "<ssid>" password "<password>"` (`:156`),
`applyWifiConfig()` single-quote wrapping (`:585,:636-648`), `applyStaticIpConfig()`
interpolating IP/gateway/iface into `ip addr`/`ip route` and rewriting `/etc/resolv.conf`
(`:685-712`). Config values come from `hal/calaos_config/calaos_config.cpp`, which does
CRC/JSON validation but no content validation — shell metacharacters in an SSID execute
arbitrary commands, typically as root on the panel (C4).

## Approach
1. Implement `ProcessRunner` in `hal/linux/`:
   `struct ProcessResult { int exitCode; std::string stdoutText; };`
   `ProcessResult run(const std::vector<std::string>& argv, int timeoutMs = ...)` —
   `fork` + `execvp` (never `/bin/sh`), pipe for stdout capture, `waitpid` with timeout +
   kill, `errno` reporting. Keep it dependency-free and small.
2. Convert all 17 `system()`/`popen()` sites to `ProcessRunner::run({"nmcli","dev","wifi",
   "connect",ssid,"password",psk})`-style argv vectors. `grep -n 'system\|popen'` to
   enumerate; the file must end with zero matches.
3. Replace the `/etc/resolv.conf` shell redirection with direct file I/O (`std::ofstream`),
   validating each DNS entry first.
4. Defense in depth at point of use: reject/log values failing basic syntax before exec —
   `inet_pton` for IPs; SSID length 1..32 bytes; interface name `[A-Za-z0-9._-]+`. (Full
   content validation at load time is CFG-01's job — do not touch `hal/calaos_config/`.)
5. Preserve existing behavior/return codes so callers see no difference on the happy path.

## Out of scope
- `hal/calaos_config/*` validation and secret-log redaction (CFG-01).
- WiFi-drop detection / `checkWifiStatus` revival (HALL-01 — it will reuse `ProcessRunner`).
- `hal/linux/linux_hal.cpp`, `linux_hal_system.cpp` (HALL-01 owns `restart()`).

## Acceptance criteria
- [ ] `grep -n 'system(\|popen(' hal/linux/linux_hal_network.cpp` → no matches.
- [ ] An SSID like `x"; touch /tmp/pwned; "` results in an nmcli failure, not command
      execution (manual test on the simulator/panel).
- [ ] WiFi connect and static-IP flows still work on a Linux target.
- [ ] `ProcessRunner` handles: binary not found, non-zero exit, timeout.

## Verification
```bash
./build_linux.sh   # [L]
# Simulator smoke [S]: provisioning flow with a WiFi config on a Linux box with nmcli;
# verify connect works and hostile SSID is rejected harmlessly.
```
