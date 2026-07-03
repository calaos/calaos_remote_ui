# WSM-01: WebSocket manager — auth classification, reconnect policy, config hardening, TLS wiring

- **Priority:** P0
- **Effort:** L
- **Phase:** 3
- **Depends on:** CORE-01, CORE-03, CORE-06, AUTH-01, NET-01, CFG-01
- **Blocks:** SP-01
- **Findings:** M9, M8 (config-update part), C1 (auth-retry timer), C2 wiring — see ../AUDIT.md
- **Status:** backlog

## Objective
Replace the substring auth-error heuristic with an explicit classifier, turn the permanent
re-provisioning trigger into bounded backoff, harden `handleConfigUpdate` field-by-field, and
wire the real TLS policy — making the WS manager robust against flaky networks and hostile
payloads.

## Files (exclusive ownership — do not edit anything else)
- `main/calaos_websocket_manager.h` / `.cpp` (modify)
- `tests/unit/test_ws_error_classification.cpp` (new)

## Context
(M9) `onClose` (`:453-540`) and `onError` (`:542-625`) contain two near-identical
errorType→httpCode mapping blocks (`:479-497` vs `:559-576`); `isAuthenticationError`
(`:1146`) substring-matches `"invalid"/"token"/"auth"` anywhere in a reason string, so a
benign disconnect ("invalid frame length"…) is misclassified as fatal auth failure and
disables auto-reconnect (`:521`); `consecutiveHandshakeErrors_ >= 3` (`:601`) permanently
forces re-provisioning (`:606-617`) — a flaky network is indistinguishable from bad
credentials. (M8) `handleConfigUpdate` does `data["brigtness"].get<int>()` (`:795` — sic,
server-side typo shim, default 80) and unguarded `std::stoi` on
`screensaver_timeout`/`screensaver_dimming` (`:809,:822`); one malformed field discards the
entire config update via the function-level catch (`:895`). `msgType = j["msg"]` type
unchecked (`:394`). Nine `handleX` methods repeat the try/catch shell (`:734-1144`).
Magic connect constants 30000/30000/5000/5 (`:177-183`). Timer: the auth-retry one-shot must
become an owned CORE-01 `Handle`. TLS: `config.verify_ssl = false // TODO` (`:180`) must come
from device config (CFG-01) via `setTlsOptions` (NET-01).

## Approach
1. Extract a pure, host-testable classifier:
   `enum class DisconnectClass { AuthFatal, Transient, ServerGone, ... };`
   `DisconnectClass classifyDisconnect(int wsCloseCode, int httpStatus, const std::string& reason)`
   — explicit close-code/status mapping (401/403/4001-style → AuthFatal; 5xx/timeout/dns →
   Transient). Reason-string matching only as documented last resort for specific known
   server strings, never generic substrings. Replace both duplicated blocks and
   `isAuthenticationError` with it.
2. Reconnect policy: Transient → exponential backoff with cap + jitter (named constants
   replacing 30000/5000/5); AuthFatal → one token-refresh/verify attempt path, and only
   after repeated *authenticated* rejections dispatch the re-provisioning event. Handshake
   errors get backoff, never permanent lockout; log the decision at each step.
3. `handleConfigUpdate`: per-field tolerant extraction via CORE-06 (`jsonu::intFieldOr`,
   `asInt`) so a bad field falls back to default while the rest of the update applies. Keep
   the `"brigtness"` shim with a comment naming the server bug. Validate `j["msg"]` type via
   `jsonu::asString`. Factor the nine try/catch shells through `jsonu::tryParse`.
4. Auth-retry one-shot → CORE-01 `Handle` member (cancelled on destruction/disconnect).
5. TLS: read `verify_ssl`/`ca_cert` from device config (CFG-01) and call `setTlsOptions`
   (NET-01); delete the hard-coded `verify_ssl = false` and the TODO.
6. Unit tests for the classifier: table of (code, status, reason) → class, including the
   regression cases "reason contains 'invalid' but close is normal" → Transient, 401 →
   AuthFatal, timeout → Transient.

## Out of scope
- Splitting the file into router/handlers (desirable, but only do it if it falls out
  naturally — the priority is behavior).
- `g_wsManager` global retirement (APP-01) and StartupPage call sites (SP-01).
- buildAuthHeaders internals (AUTH-01, merged).
- Mongoose layer (CORE-02, merged).

## Acceptance criteria
- [ ] `isAuthenticationError` substring heuristic is gone; classifier unit tests pass,
      including the benign-"invalid"-reason regression.
- [ ] Flaky-network simulation (kill/restart server 10×) → client keeps reconnecting with
      backoff and never enters re-provisioning.
- [ ] Wrong credentials → AuthFatal path → re-provisioning flow triggers (simulator).
- [ ] Config update with one malformed field (e.g. `screensaver_timeout: "abc"`) applies all
      other fields; log names the bad field.
- [ ] verify_ssl honored end-to-end from device config.

## Verification
```bash
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build   # [T]
./build_linux.sh    # [L]
# Smoke [S]: scenarios in acceptance criteria against a real/mock Calaos server.
```
