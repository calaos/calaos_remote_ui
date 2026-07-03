# AUTH-01: Shared AuthHeaderBuilder, no secret logging

- **Priority:** P1
- **Effort:** S
- **Phase:** 2
- **Depends on:** CORE-03
- **Blocks:** WSM-01, OTA-01
- **Findings:** M15 (auth-header duplication), M19 (secret logs) — see ../AUDIT.md
- **Status:** backlog

## Objective
Extract the duplicated HMAC auth-header construction into one tested builder and remove all
secret material from logs.

## Files (exclusive ownership — do not edit anything else)
- `main/auth/auth_header_builder.h` / `.cpp` (new)
- `main/calaos_websocket_manager.cpp` (replace its `buildAuthHeaders` body/call only)
- `main/ota_manager.cpp` (replace its `buildAuthHeaders` body/call only)
- `tests/unit/test_auth_header_builder.cpp` (new)

## Context
`CalaosWebSocketManager::buildAuthHeaders` (`main/calaos_websocket_manager.cpp:349-378`) and
`OtaManager::buildAuthHeaders` (`main/ota_manager.cpp:229-255`) are copy-pasted HMAC header
construction — both log the raw auth token, nonce and HMAC at DEBUG
(`calaos_websocket_manager.cpp:364-367`, `ota_manager.cpp:244-246`) (M15, M19). Both pull
credentials from `getProvisioningManager()` and use `main/hmac_authenticator.cpp`.

## Context boundary note
This ticket edits only the `buildAuthHeaders` functions and their call sites inside the two
`.cpp` files — CORE-03 (merged) owns the header files' atomics; WSM-01/OTA-01 (phase 3) own
everything else in these files. Keep the diff surgical to avoid conflicts.

## Approach
1. `main/auth/auth_header_builder.h/.cpp`: a free function or small class,
   `std::map<std::string,std::string> buildAuthHeaders(const std::string& deviceId,
   const std::string& authToken, const std::string& deviceSecret, HMACAuthenticator&)` —
   pure (inputs in, headers out), no singleton access inside, so it is host-testable.
2. Replace both duplicated bodies with calls to the shared builder (each caller fetches
   credentials from `getProvisioningManager()` as today).
3. Delete every log line that prints token/nonce/HMAC/secret; a `"auth headers built for
   device %s"` line with device id only is acceptable.
4. Unit test with fixed inputs: header set is exactly the expected names; HMAC value matches
   a precomputed vector (reuse FND-02's HMAC stubs/vectors); nonce/timestamp fields present.

## Out of scope
- Any other change in `calaos_websocket_manager.cpp` / `ota_manager.cpp` (WSM-01, OTA-01).
- `hmac_authenticator.cpp` internals (its dead `hexToBytes` is SWEEP-01).
- Provisioning manager thread safety (PROV-01).

## Acceptance criteria
- [ ] One implementation of header construction; both consumers call it (grep shows no
      duplicated HMAC assembly).
- [ ] `grep -in 'token\|hmac\|nonce\|secret' main/auth/ main/calaos_websocket_manager.cpp main/ota_manager.cpp`
      shows no log statement emitting secret values.
- [ ] Unit test passes with a known-answer vector.
- [ ] WS connection still authenticates against a real server (simulator smoke).

## Verification
```bash
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build   # [T]
./build_linux.sh    # [L]
# Smoke [S]: connect to a provisioned Calaos server; auth succeeds.
```
