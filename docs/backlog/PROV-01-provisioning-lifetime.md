# PROV-01: Provisioning requester/manager lifetime & thread safety

- **Priority:** P1
- **Effort:** M
- **Phase:** 2
- **Depends on:** none
- **Blocks:** none
- **Findings:** M3, M4, M19 (provisioning logs), m3 (dead code) — see ../AUDIT.md
- **Status:** backlog

## Objective
Give the provisioning requester deterministic join-based shutdown, make the provisioning
manager singleton and its config access thread-safe, and delete the dead verification path
and secret logs.

## Files (exclusive ownership — do not edit anything else)
- `main/provisioning_requester.h` / `.cpp` (modify)
- `main/provisioning_manager.h` / `.cpp` (modify)

## Context
`ProvisioningRequester::stopRequesting` (`main/provisioning_requester.cpp:66-111`) spins in
100 ms sleeps up to 2 s then **detaches** the thread — a detached thread can fire
`onHttpResponse` into a destroyed owner (M3). Sibling `CalaosDiscovery::stopDiscovery`
simply joins (`main/calaos_discovery.cpp:129`) — copy that semantics.
`verifyProvisioning` (~120 lines incl. `sleep_for` backoff `:381-389`) is dead code
(verification moved to WebSocket; no callers). `j.dump(0)` at `:324,:377` pretty-prints
(meant `dump()`); the full provisioning response incl. `auth_token`/`device_secret` is logged
at INFO (`:233,:241`) (M19). Unused: `DEFAULT_SERVER_PORT` (`.h:92`), extracted-but-unused
`websocketUrl`/`httpApiUrl` (`:255-262` area).
`getProvisioningManager()` (`main/provisioning_manager.cpp:286-295`) is a lazy check-then-create
race across 4 threads, and `config_` has no mutex: `resetProvisioning`/`completeProvisioning`/
`saveConfig` mutate while `getAuthToken()`/`getDeviceSecret()` (`.h:76-79`) read (M4). Dead:
`generateDeviceInfoJson()` declared, never defined (`.h:86`).

## Approach
1. Requester shutdown: replace poll-and-detach with an interruptible wait
   (`std::condition_variable::wait_for` on the stop flag instead of fixed sleeps in the
   request loop) + unconditional `join()`. HTTP calls are short-lived; the CV bounds the
   join latency. Delete the detach path entirely.
2. Delete `verifyProvisioning` and its helpers, `DEFAULT_SERVER_PORT`, the unused
   `websocketUrl`/`httpApiUrl` extraction. Fix `dump(0)` → `dump()`.
3. Replace the response INFO dumps with a redacted summary (status + device id only).
4. Manager: convert `getProvisioningManager()` to a Meyers singleton (function-local static —
   C++11 thread-safe init). Add a private `std::mutex` guarding `config_`; all getters return
   copies (`std::string` by value) and all mutators lock. Keep the public API otherwise
   identical (callers in WS manager / startup page / OTA must not need edits).
5. Note: OtaManager's own static-destruction issue is OTA-01, not here — but avoid
   introducing new static-destruction coupling (the Meyers instance must not unsubscribe
   from anything in its destructor; it currently doesn't — keep it that way).

## Out of scope
- `main/calaos_discovery.*` (DISC-01).
- TLS flags in requester HTTP calls (kept as today; WSM-01/OTA-01 wire verify_ssl for their
  channels; the provisioning HTTP request is local-network by design — document this).
- `hmac_authenticator.cpp` (SWEEP-01).

## Acceptance criteria
- [ ] `stopRequesting` always joins; `grep -n detach main/provisioning_requester.cpp` → none.
- [ ] Start/stop the requester 50× in a tight loop (temporary test main or simulator
      provisioning-screen navigation) without crash/leak.
- [ ] No log emits token/secret/response body; `dump(0)` gone.
- [ ] `config_` accessors are mutex-guarded copies; singleton is Meyers.
- [ ] Dead code removed (`verifyProvisioning`, `generateDeviceInfoJson`, unused constants).

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: full provisioning flow on simulator (fresh state → code display → server
# accepts → provisioned), plus entering/leaving the provisioning screen repeatedly.
```
