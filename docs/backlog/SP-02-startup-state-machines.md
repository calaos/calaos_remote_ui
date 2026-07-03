# SP-02: StartupPage state-machine decomposition

- **Priority:** P1
- **Effort:** L
- **Phase:** 5
- **Depends on:** SP-01, STORE-01, APP-01
- **Blocks:** none
- **Findings:** M1 (core) — see ../AUDIT.md
- **Status:** backlog

## Objective
Split the remaining ~500-line startup mega-handler into six explicit, individually-readable
phase objects coordinated by a small sequencer — behavior-preserving, validated against the
four canonical startup scenarios.

## Files (exclusive ownership — do not edit anything else)
- `main/startup_page.h` / `.cpp` (modify — becomes view + glue)
- `main/startup/*.h` / `.cpp` (new directory: phase objects + sequencer)

## Context
(M1) After SP-01's mechanical dedup/migration, `onStateChanged` still interleaves six state
machines as flat `if` blocks over cached `lastXState` members: network readiness, NTP sync,
server discovery, provisioning, WebSocket verification, and OTA gating. Each reacts to
`AppState` changes and drives labels/animations plus side effects (start discovery, start
requester, create WS manager, dispatch events). The blocks have implicit ordering ("network
before NTP before discovery…") and implicit invariants (`g_appMain->getStackView()->size()<=1`
guards at `:927,:1062` pre-SP-01 numbering — meaning "still on startup screen").
By this phase: timers are cancelable handles (CORE-01), the body runs on the LVGL thread
(SP-01), WS manager behavior is hardened (WSM-01), the WS-manager global has an accessor
(APP-01), discovery carries port/ssl (DISC-01).

## Approach
1. Create `main/startup/` with one class per phase — suggested split:
   `NetworkPhase`, `NtpPhase`, `DiscoveryPhase`, `ProvisioningPhase`, `WsVerifyPhase`,
   `OtaGatePhase` — each implementing a narrow interface, e.g.
   `class StartupPhase { virtual void onState(const AppState&) = 0;
   virtual bool isComplete() const = 0; virtual void cancel() = 0; };`
   plus a `StartupSequencer` that owns them in order, forwards state snapshots to the active
   phase, advances on completion, and supports the re-entry paths (provisioning reset,
   verify-failure → back to provisioning, network loss → back to network phase). Extract the
   exact transition set from the existing `if` conditions BEFORE coding: write the transition
   table in `main/startup/README.md` first, review it against the code, then implement.
2. Phase objects receive collaborators explicitly (view interface for labels/animations,
   dispatcher for events, discovery/requester/WS-manager factories) — no phase reaches for
   globals; StartupPage wires them.
3. StartupPage keeps: LVGL widget ownership, animations, the CORE-05 slot, and forwards
   snapshots to the sequencer. The `lastXState` diff-detection moves into the phases that
   need it.
4. Preserve every observable behavior, including timings (deferred starts), log lines
   (useful for field debugging), and the stack-size guard semantics (now via APP-01's
   accessor rather than raw `g_appMain`).
5. This is the highest-risk refactor in the backlog: land it as ONE PR, reviewed against the
   transition table, with the four scenarios re-validated + the regression scenarios from
   WSM-01 (flaky server, bad credentials).

## Out of scope
- Any behavior/UX change, timing tuning, or new states.
- WS manager / discovery / provisioning internals (all merged).
- Other pages.

## Acceptance criteria
- [ ] `startup_page.cpp` contains no state-machine `if` cascade; each phase object is
      < ~150 lines and independently readable; the transition table in
      `main/startup/README.md` matches the implementation.
- [ ] All four canonical startup scenarios (see SP-01) behave identically — plus: mid-startup
      network loss recovers; provisioning reset mid-verify returns to code screen; OTA-gate
      path still blocks/permits page transition correctly.
- [ ] ASAN-clean rapid navigation during every phase (cancel() paths exercised).
- [ ] No new globals; phases are constructor-injected.

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: the 4 canonical scenarios + the 3 re-entry scenarios above, ASAN build,
# screen recordings attached to the PR.
```
