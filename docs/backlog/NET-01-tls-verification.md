# NET-01: TLS verification on by default (HTTP + WebSocket)

- **Priority:** P0
- **Effort:** M
- **Phase:** 2
- **Depends on:** CORE-02, FND-01
- **Blocks:** WSM-01, OTA-01
- **Findings:** C2 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Verify TLS certificates by default on every HTTPS/WSS connection (embedded CA bundle), with
an explicit per-server opt-out for self-signed installations.

## Files (exclusive ownership — do not edit anything else)
- `network/http/*` (modify)
- `network/websocket/*` (modify — on top of CORE-02's single-threaded client)
- `CMakeLists.txt` (modify — include the new cmake module)
- `cmake/ca_bundle.cmake` (new) + generated/embedded bundle artifacts under `cmake/`

## Context
`network/websocket/websocket_client.cpp:490` and `network/http/http_client.cpp:396` hard-code
`opts.ca = nullptr; // Skip certificate verification`. `HttpClient` stores
`default_verify_ssl_ = true` (`http_client.cpp:14,:233`) that is never consulted — dead config
giving a false sense of security (C2). Decided policy: verify by default + per-server opt-out.
Consumers currently pass `verify_ssl = false` everywhere (`calaos_websocket_manager.cpp:180`,
`provisioning_requester.cpp:176,:400`); wiring the real flag from provisioning config is
WSM-01/OTA-01's job — this ticket makes the network layer capable and correct.

## Approach
1. `cmake/ca_bundle.cmake`: embed a CA root bundle (curl's `cacert.pem` extract or the
   distro bundle at build time) as a C array/asset for both platforms. On ESP32 check
   partition/flash cost; consider a trimmed bundle (Let's Encrypt ISRG + common commercial
   roots) — document the choice.
2. TLS init sites: pass the embedded bundle to mongoose (`opts.ca`) when verification is
   enabled. Honor the config: `HttpClient` must consult `default_verify_ssl_`/per-request
   `verify_ssl`; delete the dead path or make it live — no stored-but-ignored flags remain.
3. Add `setTlsOptions(bool verifyPeer, std::string extraCaPem = "")` to both `HttpClient`
   and `WebSocketClient`: `verifyPeer=false` = documented opt-out (self-signed);
   `extraCaPem` allows pinning a private CA per server.
4. Hostname verification: ensure mongoose's `opts.name`/SNI is set from the URL host so
   verification is real, not just chain validation.
5. Keep existing consumer call sites compiling: `verify_ssl=false` callers keep today's
   (insecure) behavior until WSM-01/OTA-01 flip them — behavior change is opt-in per
   consumer, so this ticket cannot break connectivity.

## Out of scope
- Wiring `verify_ssl` from provisioning config into WS/OTA consumers (WSM-01, OTA-01, CFG-01).
- `main/` edits of any kind.
- udp/ code.

## Acceptance criteria
- [ ] With verification enabled: `wss://` to a valid-cert server connects; to a self-signed
      server fails with a clear log; adding the server's CA via `setTlsOptions` succeeds.
- [ ] `default_verify_ssl_` is consulted (no dead config).
- [ ] Existing callers (all passing verify_ssl=false today) behave exactly as before.
- [ ] ESP32 build fits: report flash-size delta of the embedded bundle in the PR.

## Verification
```bash
./build_linux.sh    # [L]
idf.py build        # [E] — bundle embedding + mbedtls path
# Smoke [S]: Linux app vs (a) wss with Let's Encrypt cert → connects; (b) self-signed → fails;
# (c) self-signed + setTlsOptions(extraCa) → connects.
```
