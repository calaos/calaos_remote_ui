# CFG-01: Provisioning config validation, secret redaction, verify_ssl field

- **Priority:** P0
- **Effort:** S
- **Phase:** 2
- **Depends on:** CORE-06
- **Blocks:** WSM-01, OTA-01
- **Findings:** C4 (validation part), M19 (wifi_password log), C2 (verify_ssl field) — see ../AUDIT.md
- **Status:** backlog

## Objective
Validate the content of provisioning-config fields at load time, stop logging the plaintext
WiFi password, and parse the new per-server `verify_ssl` flag consumed by WSM-01/OTA-01.

## Files (exclusive ownership — do not edit anything else)
- `hal/calaos_config/calaos_config.cpp` / `.h` (modify)
- `hal/calaos_config/device_config.h` (modify)
- `tests/unit/test_calaos_config.cpp` (new)

## Context
`calaos_config.cpp` validates CRC and JSON well-formedness but not field content (C4 —
the shell-injection sink is fixed independently in CORE-04; this is defense in depth at the
source). `:105` logs the entire raw JSON payload including plaintext `wifi_password`, even
though a redacted line exists 50 lines later (`:157-159`) (M19). The TLS policy (C2) needs a
per-server `verify_ssl` boolean (default `true`) in the device config schema.

## Approach
1. Field validation on load, using CORE-06 helpers (`main/json_utils.h`) for tolerant typed
   extraction: SSID 1..32 bytes; PSK 8..63 chars (or empty for open networks); IPs/gateway/
   DNS via `inet_pton`; interface/hostname charset. Invalid field → reject the config with a
   specific log naming the field (never echoing secret values), matching today's
   CRC-failure error path.
2. Delete the raw-payload log at `:105`; keep/extend the redacted summary. Grep the whole
   directory for other payload dumps.
3. Add `verify_ssl` (bool, default `true`) + optional `ca_cert` (PEM string, default empty)
   to `device_config.h` and the parser. Absent field → default (backward compatible with
   existing provisioned devices).
4. Unit tests: valid config round-trip; each invalid field rejected; missing `verify_ssl`
   defaults true; hostile SSID (shell metacharacters) — accepted or rejected per the chosen
   charset rule, but never breaks parsing; CRC failure path unchanged.

## Out of scope
- Consuming `verify_ssl` (WSM-01, OTA-01).
- The exec-layer hardening (CORE-04).
- Provisioning request/response handling (`main/provisioning_*` — PROV-01).

## Acceptance criteria
- [ ] No log statement in `hal/calaos_config/` can emit `wifi_password` or raw payload.
- [ ] Malformed field values are rejected with a field-specific, secret-free error.
- [ ] `verify_ssl`/`ca_cert` parsed with correct defaults; existing configs (no field) load
      unchanged.
- [ ] Unit tests pass.

## Verification
```bash
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build   # [T]
./build_linux.sh    # [L]
```
