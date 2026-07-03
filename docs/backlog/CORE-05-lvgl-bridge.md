# CORE-05: Reusable "apply-on-LVGL-thread" store binding

- **Priority:** P0
- **Effort:** M
- **Phase:** 1
- **Depends on:** none
- **Blocks:** SP-01, STORE-01, OTA-02
- **Findings:** M2 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Generalize the one correct cross-thread UI-update pattern (OtaUpdateScreen) into a small
reusable helper, so store subscribers stop mutating LVGL from the dispatcher thread under a
lossy `tryLock`.

## Files (exclusive ownership — do not edit anything else)
- `main/ui/lvgl_bridge.h` / `.cpp` (new)
- `main/ui/README.md` (new — pattern documentation)

## Context
Every `AppStore::subscribe` callback runs on the AppDispatcher worker thread (see AUDIT.md
threading model). All subscribers except `OtaUpdateScreen` reach into LVGL under
`HAL::getDisplay().tryLock(100)`, silently dropping the UI update when the lock is contended
(M2). `OtaUpdateScreen` instead snapshots state (atomics + mutex-guarded string) and applies
it in `update()` on the LVGL thread — the pattern to generalize. Read
`main/ota_update_screen.cpp` before designing (read-only; do not modify it here).

## Approach
1. Design a small header-first utility, e.g.:
   - `template <typename Snapshot> class UiStateSlot` — `publish(Snapshot)` from any thread
     (mutex-guarded copy + dirty flag); `bool consume(Snapshot& out)` from the LVGL thread
     (returns true once per publish, latest-wins coalescing).
   - `void postToLvgl(std::function<void()> fn)` — one-shot marshaling onto the LVGL thread
     (via `lv_async_call` with a heap functor, mirroring the mechanism already used in
     `main/lvgl_timer.cpp`; keep the implementation independent — lvgl_timer.cpp is owned by
     CORE-01 this phase).
2. Document in `main/ui/README.md`: why subscribers must not call `lv_*` directly, the
   subscribe→publish→consume-in-render-loop pattern with a code example, latest-wins
   semantics, and the migration checklist for STORE-01/SP-01.
3. No consumers are migrated in this ticket; compile-only integration (add the new files to
   the build via the existing glob in `cmake/sources.cmake` if sources are globbed — check;
   if sources are listed explicitly, adding the two paths there is permitted and is this
   ticket's only edit outside `main/ui/`).

## Out of scope
- Migrating any subscriber (STORE-01, SP-01, OTA-02).
- Touching `main/ota_update_screen.cpp` (reference only; OTA-02 will migrate it).
- Modifying flux/ (STORE-01 adds the debug assert).

## Acceptance criteria
- [ ] `UiStateSlot::publish` from a non-LVGL thread + `consume` on the LVGL thread is
      race-free (mutex) and coalesces bursts (latest snapshot wins).
- [ ] `postToLvgl` executes the functor exactly once on the LVGL thread, including under
      rapid-fire usage; functor lifetime is heap-managed (no dangling stack captures).
- [ ] README documents the pattern with a worked example.
- [ ] Builds on both platforms with zero consumers (dead-code warnings acceptable only if the
      ratchet allows).

## Verification
```bash
./build_linux.sh   # [L]
```
