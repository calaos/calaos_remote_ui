# CORE-02: WebSocketClient — all mongoose calls on the poll thread

- **Priority:** P0
- **Effort:** L
- **Phase:** 1
- **Depends on:** none
- **Blocks:** NET-01
- **Findings:** C5 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Route every `mg_*` call through the mongoose poll thread via an outbox/command queue,
eliminating the cross-thread data race and potential use-after-free on `conn_`.

## Files (exclusive ownership — do not edit anything else)
- `network/websocket/*` (websocket_client.cpp/.h and any types header in that directory)

## Context
`mg_mgr_poll` runs on `serviceThread` (`network/websocket/websocket_client.cpp:352-361`), but
`sendText/sendBinary/ping` call `mg_ws_send` directly from the caller's thread via
`processOutgoingMessages()` (`:329-350,:259`), and `disconnect()` mutates `conn_->is_closing`
from the caller thread (`:187-191`). Mongoose's contract requires all `mg_*` calls on the
thread running the manager. Additionally `scheduleReconnect()` accesses `current_config_` /
`reconnect_attempts_` without `config_mutex_` when invoked from the event handler
(`:308-327,:471,:593`).

## Approach
1. Add a mutex-guarded command queue (send-text / send-binary / ping / close). Public methods
   enqueue and wake the poll loop; they no longer touch `conn_` or call `mg_*`.
2. Wake mechanism: `mg_wakeup()` (mongoose ≥7.9) if available in the vendored copy
   (`components/mongoose`, 7.8.x — check first); otherwise use `mg_mgr_poll`'s short timeout
   plus a self-pipe/socketpair, or reduce poll timeout to ~50 ms and document the latency.
3. Drain the queue inside the poll thread (either in the `MG_EV_POLL`/wakeup handler or right
   after `mg_mgr_poll` returns) — that code is the ONLY caller of `mg_ws_send` /
   `is_closing = 1`.
4. `disconnect()` becomes an enqueued close command + join semantics unchanged for the
   public API. Verify destructor ordering: stop reconnect thread, enqueue close, join service
   thread, then free.
5. Fix `scheduleReconnect()` locking: take `config_mutex_` (or make the fields atomic) on all
   paths including the event-handler invocation.
6. Public API must remain source-compatible — `main/calaos_websocket_manager.cpp` is NOT in
   this ticket's file set and must not need edits.

## Out of scope
- TLS options (NET-01 — will land on top of this).
- `network/http/*`, `network/udp/*`.
- Any change to `main/` consumers.

## Acceptance criteria
- [ ] `grep -n 'mg_' network/websocket/*.cpp` shows `mg_*` invocations only in the poll
      thread's code paths (init/poll/drain/event handler).
- [ ] `sendText` from a foreign thread while the poll loop is mid-`mg_mgr_poll` is safe
      (queue+wakeup); no direct `conn_` access outside the poll thread.
- [ ] Reconnect fields accessed under lock (or atomic) on every path.
- [ ] Public header API unchanged (no edits needed outside `network/websocket/`).

## Verification
```bash
./build_linux.sh    # [L]
# Simulator smoke [S]: run the Linux app against a Calaos server (or `websocat -s 5454`
# echo server): connect, exchange messages rapidly while toggling widgets, disconnect server
# mid-traffic → client reconnects, no crash/UAF (run under ASAN if available:
# cmake -DCMAKE_CXX_FLAGS=-fsanitize=address ...).
```
