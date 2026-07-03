# STORE-01: Migrate remaining store subscribers to the LVGL-thread pattern

- **Priority:** P1
- **Effort:** M
- **Phase:** 4
- **Depends on:** CORE-05, PROTO-01, NAV-01
- **Blocks:** SP-02
- **Findings:** M2 — see ../AUDIT.md
- **Status:** backlog

## Objective
Convert every remaining store subscriber that mutates LVGL from the dispatcher thread to the
CORE-05 snapshot/apply pattern, and add a debug assertion that catches future regressions.

## Files (exclusive ownership — do not edit anything else)
- `main/calaos_page.h` / `.cpp` (modify)
- `main/about_page.h` / `.cpp` (modify)
- `main/notification_toast.h` / `.cpp` (modify)
- `flux/app_dispatcher.h` / `.cpp` (modify — debug assert + doc comment only)

## Context
(M2) All `AppStore::subscribe` callbacks run on the AppDispatcher worker thread; these three
subscribers mutate LVGL under `HAL::getDisplay().tryLock(100)`, silently dropping updates on
contention (e.g. `calaos_page.cpp:361-396`). CORE-05 (merged) provides `UiStateSlot` +
`postToLvgl` and `main/ui/README.md` documents the migration checklist. Already migrated or
owned elsewhere: `ota_update_screen` (reference pattern; typed by OTA-02),
`startup_page` (SP-01, same phase, different files), `screensaver` (partially, UI-01),
`ota_manager` (not a UI subscriber), `calaos_widget`'s per-widget subscriptions
(`onAppStateChanged` — migrate here ONLY if it touches LVGL from the dispatcher thread;
check `main/calaos_widget.cpp:125-128` — if widget updates flow through CalaosPage's render
path instead, document that and leave widgets alone; `calaos_widget.*` is NOT in this file
set — if migration is needed there, stop and split a follow-up ticket instead of editing it).

## Approach
1. For each of the three subscribers: callback stores a snapshot of the fields it consumes
   into a `UiStateSlot`; the LVGL-side consumption happens in the existing render/update
   hook (`CalaosPage::render` runs every frame — consume there; give AboutPage and
   NotificationToast an equivalent periodic LVGL timer or hook into their page render).
   Remove the `tryLock` UI mutation from the callback path.
2. NotificationToast: toast show/hide timing must stay correct when several notifications
   arrive in a burst — the slot needs a small queue semantics for this one (latest-wins would
   drop toasts; a bounded deque snapshot is fine).
3. `flux/app_dispatcher.cpp`: expose the worker thread id (or an `isDispatcherThread()`
   helper); in debug builds, add an assertion hook that UI code can call
   (`FLUX_ASSERT_NOT_DISPATCHER()`), and document in the header that subscribers MUST NOT
   call `lv_*` directly. Wire the assert into the three migrated consumers' apply paths as
   a self-check.
4. CalaosPage caution: `onStateChanged` also rebuilds pages on config change
   (PROTO-01-hardened) — the rebuild itself must move to the LVGL thread with the rest;
   verify the `lastConfigJson` compare still coalesces correctly with latest-wins snapshots.

## Out of scope
- `startup_page` (SP-01), `screensaver` (UI-01, merged), `ota_update_screen` (OTA-02).
- `calaos_widget.*` (see Context — follow-up ticket if needed).
- Store/dispatcher core redesign (the copy-under-lock notify design is healthy — AUDIT.md
  "Explicitly healthy").

## Acceptance criteria
- [ ] No `tryLock`-wrapped LVGL mutation remains in the three subscribers' callbacks (grep
      `tryLock` in owned files → only legitimate non-subscriber uses, ideally none).
- [ ] Debug assert fires if a subscriber calls `lv_*` on the dispatcher thread (verified by
      a deliberate temporary violation during development).
- [ ] Soak: rapid state updates (server toggling many IOs) drop no UI refreshes — final UI
      state always matches server state; toasts all appear.
- [ ] Page-config change still rebuilds pages correctly.

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: simulator + server script toggling 20 IOs rapidly for 60 s; UI ends consistent;
# burst of 5 notifications → 5 toasts; about page open during updates.
```
