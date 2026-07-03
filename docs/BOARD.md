# Refactoring Board — calaos_remote_ui

Backlog of independent, agent-executable refactoring tickets derived from the technical-debt
audit in [AUDIT.md](AUDIT.md). Each ticket lives in [`backlog/`](backlog/) and is
self-contained: an autonomous dev agent needs only its ticket file + `AUDIT.md`.

## Rules

1. **One ticket = one worktree = one PR.** A ticket may only edit the files listed in its
   "Files (exclusive ownership)" section.
2. **Phase gate:** Phase N+1 starts only when every Phase N ticket is Done (merged). Within
   a phase, all tickets own disjoint file sets and may run in parallel worktrees.
3. **Warning ratchet:** after FND-01 lands, the project-code warning count
   (`./build_linux.sh 2>&1 | grep -c 'warning:'`, filtered to `main/ hal/ network/ flux/`)
   must not increase for files a ticket owns. Baseline is recorded in FND-01.
4. **Verification tiers** (each ticket lists which apply):
   - **[L]** Linux build: `./build_linux.sh` — required for every ticket
   - **[T]** unit tests: `cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build`
   - **[E]** ESP32 build: `idf.py build` (or `boards/<board>/build.sh`) — required whenever
     files under `hal/esp32/`, `boards/`, `cmake/`, root `CMakeLists.txt` or `sdkconfig*` change
   - **[S]** Linux simulator smoke: manual scenario listed in the ticket
   - **[D]** device-only: manual on-hardware step, recorded in the PR
5. **Status flow:** `backlog → ready → in-progress → review → done`. Update BOTH the ticket
   header's `Status:` field and the Kanban section below.
6. **Scope creep:** anything discovered outside a ticket's file list becomes a new ticket
   (or a note in Follow-ups below), never an in-place edit.

## Kanban

### Ready (deps merged, phase open)
- [FND-01](backlog/FND-01-build-flags.md) — Enable -Wall/-Wextra, honor build type, fix build script
- [FND-02](backlog/FND-02-test-harness.md) — Standalone host unit-test harness

### Backlog
- All Phase 1–5 tickets (see Phase plan below) — move to Ready as phases open.

### In Progress
_(ticket — worktree/branch — agent)_

### In Review

### Done

## Phase plan (dependency order)

| Phase | Ticket | Title | Prio | Effort | Depends on | Verify |
|-------|--------|-------|------|--------|------------|--------|
| 0 | [FND-01](backlog/FND-01-build-flags.md) | Build flags & script hygiene | P1 | S | — | L,E |
| 0 | [FND-02](backlog/FND-02-test-harness.md) | Host unit-test harness | P1 | M | — | T |
| 1 | [FND-03](backlog/FND-03-pr-ci.md) | PR CI + board matrix fixes | P1 | S | FND-01, FND-02 | — |
| 1 | [CORE-01](backlog/CORE-01-lvgl-timer-handles.md) | LvglTimer cancelable one-shot handles | P0 | M | FND-02 | L,T |
| 1 | [CORE-02](backlog/CORE-02-ws-client-single-thread.md) | Mongoose single-threaded WS client | P0 | L | — | L,S |
| 1 | [CORE-03](backlog/CORE-03-atomic-flags.md) | Cross-thread flags → atomics | P0 | S | — | L,E |
| 1 | [CORE-04](backlog/CORE-04-shell-exec-hardening.md) | Shell-exec hardening (Linux HAL) | P0 | M | — | L,S |
| 1 | [CORE-05](backlog/CORE-05-lvgl-bridge.md) | LVGL-thread UI bridge | P0 | M | — | L |
| 1 | [CORE-06](backlog/CORE-06-json-utils.md) | Safe JSON helpers | P0 | S | FND-02 | L,T |
| 2 | [NET-01](backlog/NET-01-tls-verification.md) | TLS verification by default | P0 | M | CORE-02, FND-01 | L,E,S |
| 2 | [CFG-01](backlog/CFG-01-config-validation.md) | Config validation + secret redaction | P0 | S | CORE-06 | L,T |
| 2 | [AUTH-01](backlog/AUTH-01-auth-header-builder.md) | Shared auth-header builder | P1 | S | CORE-03 | L,T,S |
| 2 | [WID-01](backlog/WID-01-widget-dedup.md) | Widget base dedup | P1 | L | CORE-01 | L,S |
| 2 | [PROV-01](backlog/PROV-01-provisioning-lifetime.md) | Provisioning lifetime & thread safety | P1 | M | — | L,S |
| 2 | [DISC-01](backlog/DISC-01-discovery-correctness.md) | Discovery correctness | P1 | S | FND-02 | L,T,S |
| 2 | [NAV-01](backlog/NAV-01-stackview-transitions.md) | StackView queued transitions | P1 | M | — | L,S |
| 2 | [BRD-01](backlog/BRD-01-boards-relay-dedup.md) | Boards/relay dedup | P2 | M | FND-01 | L,E |
| 3 | [WSM-01](backlog/WSM-01-ws-manager-hardening.md) | WS auth/reconnect/config hardening | P0 | L | CORE-01,03,06; AUTH-01; NET-01; CFG-01 | L,T,S |
| 3 | [PROTO-01](backlog/PROTO-01-protocol-robustness.md) | Protocol parse robustness | P0 | M | CORE-06, FND-02 | L,T,S |
| 3 | [OTA-01](backlog/OTA-01-ota-lifetime.md) | OTA lifetime + HAL accessor + TLS | P1 | M | CORE-03; AUTH-01; NET-01; CFG-01 | L,E,S,D |
| 3 | [HALE-01](backlog/HALE-01-esp32-network-robustness.md) | ESP32 network robustness | P1 | L | CORE-03 | E,D |
| 3 | [HALE-02](backlog/HALE-02-esp32-lifecycle-deinit.md) | ESP32 lifecycle deinit | P1 | M | CORE-03, BRD-01 | E,D |
| 3 | [HALL-01](backlog/HALL-01-linux-hal-lifecycle.md) | Linux HAL lifecycle + WiFi drop | P1 | M | CORE-03, CORE-04 | L,S |
| 3 | [UI-01](backlog/UI-01-format-utils-dedup.md) | Font/date util + screensaver dedup | P2 | M | CORE-01, WID-01 | L,S |
| 4 | [SP-01](backlog/SP-01-startup-page-dedup.md) | StartupPage dedup + migrations | P1 | L | CORE-01,05; WSM-01; OTA-01; DISC-01 | L,S |
| 4 | [STORE-01](backlog/STORE-01-subscribers-lvgl-thread.md) | Store subscribers → LVGL thread | P1 | M | CORE-05; PROTO-01; NAV-01 | L,S |
| 4 | [APP-01](backlog/APP-01-appmain-lifecycle.md) | AppMain lifecycle/teardown | P1 | M | OTA-01, CORE-03 | L,E,S |
| 4 | [OTA-02](backlog/OTA-02-typed-ota-commands.md) | Typed OTA screen commands | P2 | S | OTA-01, CORE-05 | L,S |
| 5 | [SP-02](backlog/SP-02-startup-state-machines.md) | StartupPage state-machine split | P1 | L | SP-01; STORE-01; APP-01 | L,S |
| 5 | [SWEEP-01](backlog/SWEEP-01-dead-code-sweep.md) | Dead code & cruft sweep | P2 | M | Phase 4 done | L,E,S |
| 5+ | [DEP-01](backlog/DEP-01-devendor-deps.md) | De-vendor dependencies (OPTIONAL) | P3 | L | SWEEP-01 | L,E |

**Effort:** S ≤ half day · M ≈ one day · L = large single session.
**Priority:** P0 = security/crash/UAF · P1 = major robustness/maintainability · P2 = duplication/cruft · P3 = optional.

## Findings coverage (AUDIT.md → tickets)

| Finding | Ticket(s) |
|---------|-----------|
| C1 timers UAF | CORE-01 (core) + WID-01, WSM-01, UI-01, SP-01 (call sites) |
| C2 TLS disabled | NET-01 (core) + CFG-01 (field), WSM-01, OTA-01 (wiring) |
| C3 data races | CORE-03 |
| C4 shell injection | CORE-04 (exec) + CFG-01 (validation) |
| C5 mongoose threads | CORE-02 |
| M1 StartupPage god object | SP-01 (dedup) + SP-02 (decomposition) |
| M2 subscribers on dispatcher thread | CORE-05 (pattern) + SP-01, STORE-01, OTA-02 (migration) |
| M3 requester detach | PROV-01 |
| M4 provisioning manager races | PROV-01 |
| M5 AppMain teardown UAF | APP-01 |
| M6 OTA lifetime | OTA-01 |
| M7 StackView drops | NAV-01 |
| M8 JSON robustness | CORE-06 (helpers) + PROTO-01, WSM-01, CFG-01 |
| M9 auth heuristic | WSM-01 |
| M10 discovery port/ssl | DISC-01 |
| M11 ESP_ERROR_CHECK panics | HALE-01 + HALE-02 (nvs site) |
| M12 WiFi drop unhandled | HALE-01 (ESP32) + HALL-01 (Linux) |
| M13 deinit leaks | HALE-01, HALE-02, HALL-01 |
| M14 widget copy-paste | WID-01 |
| M15 auth/fonts/TZ duplication | AUTH-01 + UI-01 |
| M16 boards/relay duplication | BRD-01 |
| M17 build flags | FND-01 |
| M18 tests/CI | FND-02 + FND-03 |
| M19 secrets logged | AUTH-01, CFG-01, PROV-01 |
| m1–m12 minors | bundled — see each minor's arrow in AUDIT.md |

## Conflict matrix (file ownership per phase)

A phase is valid iff no file appears under two of its tickets. Cross-phase re-touching is
fine (sequential). If editing a ticket's file list would create an intra-phase overlap, move
the ticket to the next phase instead.

| Phase | Ticket | Owned area |
|-------|--------|-----------|
| 0 | FND-01 | `CMakeLists.txt`, `build_linux.sh` |
| 0 | FND-02 | `tests/**` (new) |
| 1 | FND-03 | `.github/workflows/*`, `boards/ci-boards.json` |
| 1 | CORE-01 | `main/lvgl_timer.*` (+ its test) |
| 1 | CORE-02 | `network/websocket/*` |
| 1 | CORE-03 | `main/calaos_websocket_manager.*`, `main/ota_manager.h`, `hal/esp32/esp32_hal_network.*`, `hal/esp32/esp32_hal.*`, `hal/linux/linux_hal.*` |
| 1 | CORE-04 | `hal/linux/linux_hal_network.*`, `hal/linux/process_runner.*` (new) |
| 1 | CORE-05 | `main/ui/lvgl_bridge.*` (new), `main/ui/README.md` |
| 1 | CORE-06 | `main/json_utils.h` (new) (+ its test) |
| 2 | NET-01 | `network/http/*`, `network/websocket/*`, `CMakeLists.txt`, `cmake/ca_bundle.cmake` (new) |
| 2 | CFG-01 | `hal/calaos_config/**` |
| 2 | AUTH-01 | `main/auth/**` (new), `main/calaos_websocket_manager.cpp`, `main/ota_manager.cpp` (buildAuthHeaders only) |
| 2 | WID-01 | `main/widgets/**` minus `clock_widget.*`, `main/calaos_widget.*`, `main/widget_factory.cpp` |
| 2 | PROV-01 | `main/provisioning_requester.*`, `main/provisioning_manager.*` |
| 2 | DISC-01 | `main/calaos_discovery.*` |
| 2 | NAV-01 | `main/stack_view.*` |
| 2 | BRD-01 | `boards/**`, `cmake/board_config.h.in`, `hal/*/…_hal_relay.*` |
| 3 | WSM-01 | `main/calaos_websocket_manager.*` |
| 3 | PROTO-01 | `main/calaos_protocol.*`, `main/calaos_page.cpp` |
| 3 | OTA-01 | `main/ota_manager.*`, `hal/hal_ota.h`, `hal/hal.{h,cpp}`, `main/startup_page.cpp` (one line) |
| 3 | HALE-01 | `hal/esp32/esp32_hal_network.*` |
| 3 | HALE-02 | `hal/esp32/*` minus network (and minus `hal/hal.h` — OTA-01's) |
| 3 | HALL-01 | `hal/linux/linux_hal.cpp`, `linux_hal_network.*`, `linux_hal_system.cpp` |
| 3 | UI-01 | `main/ui/format_utils.*` (new), `main/screensaver.*`, `main/widgets/clock_widget.*`, `main/image_sequence_animator.cpp` |
| 4 | SP-01 | `main/startup_page.*` |
| 4 | STORE-01 | `main/calaos_page.*`, `main/about_page.*`, `main/notification_toast.*`, `flux/app_dispatcher.*` |
| 4 | APP-01 | `main/app_main.*`, `main/main.cpp`, `g_wsManager` sites (`main/calaos_websocket_manager.*` global only, `main/calaos_widget.cpp` consumers, `main/startup_page.cpp` one method) |
| 4 | OTA-02 | `main/ota_update_screen.*`, `main/ota_manager.cpp` |
| 5 | SP-02 | `main/startup_page.*`, `main/startup/**` (new) |
| 5 | SWEEP-01 | inventory-driven, EXCLUDES all SP-02 files |

Known intra-phase adjacencies (validated non-overlapping, keep them that way):
- **Phase 1:** CORE-02 (`network/`) vs CORE-03 (`main/calaos_websocket_manager.*`) split on
  directory. Tests never conflict (glob discovery, one new file per ticket).
- **Phase 2:** NET-01 and BRD-01 both touch `cmake/` — different files
  (`ca_bundle.cmake` new vs `board_config.h.in`). AUTH-01 edits only the `buildAuthHeaders`
  functions inside two files whose headers CORE-03 (phase 1, merged) touched.
- **Phase 3:** HALE-02 must not touch `hal/hal.h` (OTA-01 owns it) nor
  `esp32_hal_network.*` (HALE-01). OTA-01's `startup_page.cpp` edit is a single call-site
  line at `:542` (SP-01 owns the file next phase).
- **Phase 4:** APP-01 edits `main/startup_page.cpp` (g_wsManager assignments) and
  `main/calaos_websocket_manager.*` (global definition) while SP-01/OTA-02 run — the owned
  regions are disjoint (SP-01: everything except the g_wsManager assignment inside
  `connectWebSocketForVerification()`; OTA-02 in `ota_manager.cpp`: command-producing sites
  only). **If either diff grows beyond its region, serialize: land SP-01 and OTA-02 first,
  then APP-01.**
- **Phase 5:** SP-02 and SWEEP-01 disjoint by SWEEP-01's explicit exclusion list.

## Follow-ups / not scheduled

- ESP32 build on PR CI (heavy image pulls — revisit after FND-03 stabilizes).
- Incremental page diffing in `CalaosPage` (today: any `pages_json` byte change rebuilds all
  pages — coarse but correct; see PROTO-01 notes).
- Real Linux OTA (swupdate/RAUC) — `linux_ota.cpp` remains a simulation stub (see HALL-01).
- Per-widget follow-up if STORE-01's analysis shows `calaos_widget.cpp` subscribers touch
  LVGL from the dispatcher thread (see STORE-01 Context).
- FreeRTOS task priority/stack budget review (all tasks share priority 5 — AUDIT.md §6).
