# SP-01: StartupPage — dedup, dead-code removal, timer + LVGL-thread migration

- **Priority:** P1
- **Effort:** L
- **Phase:** 4
- **Depends on:** CORE-01, CORE-05, WSM-01, OTA-01, DISC-01
- **Blocks:** SP-02
- **Findings:** M1 (dedup part), C1 (timer sites), M2 (subscriber) — see ../AUDIT.md
- **Status:** backlog

## Objective
Shrink and de-risk `startup_page.cpp` WITHOUT changing its state-machine behavior: extract
the duplicated blocks into private methods, delete dead code, migrate all one-shot timers to
cancelable CORE-01 handles, and run `onStateChanged` on the LVGL thread via CORE-05 — paving
the way for SP-02's decomposition.

## Files (exclusive ownership — do not edit anything else)
- `main/startup_page.h` / `.cpp` (modify)

## Context
(M1) `onStateChanged` (`main/startup_page.cpp:391-1088`, ~700 lines) contains three
near-verbatim copies of the "create WS manager + set g_wsManager + connect + dispatch
ProvisioningVerifyFailed on failure" block (`:676-699`, `:759-778`, `:836-854`), two copies
of "start provisioning requester (deferred)" (`:709-722`, `:801-814`), and three copies of
the WiFi/Ethernet/static-IP status-string construction (`:43-64`, `:455-465`, `:481-505`).
Dead: raw members `spinner/statusLabel/provCodeLabel/provCodeValue/provIpValue`
(`startup_page.h:34-38`), commented-out `testButtonCb` (`:1090-1116`).
(C1) ~7 `LvglTimer::createOneShot([this]{...})` sites (`:678,:902,:981,:990,:1015,:1036,:1060`)
capture `this` with no cancellation — the destructor (`:113`) stops threads but cannot cancel
these; CORE-01 (merged) provides `Handle`.
(M2) The store subscription currently runs the whole 700-line body on the dispatcher thread
under the display lock; CORE-05 (merged) provides the snapshot/apply pattern
(see `main/ui/README.md`).
Note: OTA-01 already changed the `createOta()` call at `:542` to `HAL::getOta()` — rebase on
that. `g_wsManager` global stays for now (APP-01 retires it — do not touch its semantics,
only wrap the duplicated assignment inside the extracted method).

## Approach
1. Extract private methods (identical behavior, one copy each):
   `connectWebSocketForVerification()`, `startProvisioningRequesterDeferred(uint32_t delayMs)`,
   `std::string buildNetworkStatusMessage(const AppState&)`. Replace the 8 duplicated blocks.
2. Delete dead members and `testButtonCb`; name the magic delays/offsets used by this file
   as file-local `constexpr` (10/100/800/1000/3000 ms, layout offsets).
3. Replace every `createOneShot` fire-and-forget with `LvglTimer::Handle` members (a small
   `std::vector<Handle>` for the transient ones is fine) so destruction cancels them all —
   removes the dangling-`this` class of bugs.
4. Subscriber migration: the store callback now only publishes the state snapshot into a
   CORE-05 `UiStateSlot<AppState>`; a member LVGL timer (or the page's render hook) consumes
   it and runs the existing `onStateChanged` body on the LVGL thread. Drop the now-redundant
   `tryLock` wrapping inside the body (it runs on the LVGL thread already). Latest-wins
   coalescing is acceptable — the body already works off full-state diffs
   (`lastXState` members).
5. NO state-machine restructuring: the flat `if` blocks stay in order (SP-02's job). Resist
   any behavior "improvement" — the diff must be reviewable as pure mechanical refactor.

## Out of scope
- Splitting the state machines into phase objects (SP-02).
- `g_wsManager`/`g_appMain` globals (APP-01).
- WS manager internals (WSM-01, merged), discovery (DISC-01, merged).

## Acceptance criteria
- [ ] The three duplicated block families exist once each; file shrinks substantially
      (expect roughly 200+ lines removed).
- [ ] Zero fire-and-forget one-shots: destructor cancels everything; navigating away from
      StartupPage mid-startup (rapid pop) causes no crash under ASAN.
- [ ] `onStateChanged` body executes on the LVGL thread (assert via the STORE-01-style debug
      check or a thread-id log in debug builds).
- [ ] Full startup flows behave identically (see Verification scenarios).

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S] — the four canonical startup scenarios on the simulator:
#  1. cold start, no provisioning → discovery → provisioning code screen → server accepts → main page
#  2. already provisioned, server up → direct connect → main page
#  3. already provisioned, server down → retry/backoff screen → server up → main page
#  4. provisioned, bad credentials → verify-failed → re-provisioning flow
# Each must match pre-refactor behavior (record short screen captures in the PR).
```
