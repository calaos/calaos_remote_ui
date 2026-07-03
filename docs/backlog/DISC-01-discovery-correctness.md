# DISC-01: CalaosDiscovery correctness

- **Priority:** P1
- **Effort:** S
- **Phase:** 2
- **Depends on:** FND-02
- **Blocks:** SP-01
- **Findings:** M10 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Make discovery actually propagate the announced server port/SSL flag, validate IPs properly,
and unit-test the announce parsing.

## Files (exclusive ownership — do not edit anything else)
- `main/calaos_discovery.h` / `.cpp` (modify)
- `tests/unit/test_discovery_parse.cpp` (new)

## Context
`CalaosServerFoundData` is populated with `serverIp` only (`main/calaos_discovery.cpp:52-53`,
`:252-253`); `serverPort`/`serverSsl` remain defaults — downstream consumers
(`startup_page.cpp:713-714`, provisioning requester) read them, and it works only because the
defaults (5454/false) coincide with typical servers (M10). A discovered non-default server
would be unreachable. IP "validation" is `message.find('.') != npos` (`:243`). Minor: the
`CALAOS_SERVER_IP` env back-door sits inline in the discovery path (`:30`) — keep the
feature (the simulator relies on it via `main.cpp --server-ip`) but isolate/document it;
`discovering_.store(false)` inside `onUdpDataReceived` (`:258`) is fine but deserves a
comment on why no mutex is needed there. The self-broadcast-filter `substr` logic
(`:205,:215,:219`) is subtle — cover it with tests rather than rewriting it.

## Approach
1. Extract a pure, host-testable parse function, e.g.
   `static std::optional<CalaosServerFoundData> parseAnnounce(const std::string& datagram)`,
   handling: the self-broadcast filter, IP extraction + `inet_pton` validation, and
   port/SSL extraction when present in the announce payload (inspect the real Calaos
   discovery reply format first — check how the server formats it; if the announce genuinely
   carries no port/SSL, then make the struct's defaults explicit named constants and document
   that downstream defaults are intentional, not accidental).
2. Populate `serverPort`/`serverSsl` in both code paths (`:52-53` env path, `:252-253` UDP
   path) — from the parsed announce or the documented named defaults.
3. Replace `find('.')` with `inet_pton`-based validation.
4. Keep the env-var back-door but move it behind a clearly named helper
   (`applySimulatorOverride()`) with a comment pointing at `main.cpp --server-ip`.
5. Unit tests for `parseAnnounce`: valid announce, self-broadcast packet filtered, garbage
   datagram, short packet (9-byte edge from the `size<9` guard at `:205`), invalid IP
   rejected, port/SSL extraction (or defaults) — pin today's behavior.

## Out of scope
- StartupPage's consumption of discovery results (SP-01).
- UDP socket layer (`network/udp/*`).
- Thread start/stop redesign (its join semantics are already correct — the reference for PROV-01).

## Acceptance criteria
- [ ] `CalaosServerFoundData` always carries explicit ip+port+ssl (no silent
      default-coincidence).
- [ ] `inet_pton` validation; `"...."`-style strings rejected.
- [ ] Parse function unit-tested including the self-broadcast filter edge cases.
- [ ] Simulator discovery still finds a real server (smoke).

## Verification
```bash
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build   # [T]
./build_linux.sh    # [L]
# Smoke [S]: simulator on a LAN with a Calaos server → discovered; with --server-ip override → used.
```
