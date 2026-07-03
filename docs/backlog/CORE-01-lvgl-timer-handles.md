# CORE-01: LvglTimer — thread-safe, cancelable one-shot handles

- **Priority:** P0
- **Effort:** M
- **Phase:** 1
- **Depends on:** FND-02
- **Blocks:** WID-01, WSM-01, UI-01, SP-01
- **Findings:** C1 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Make one-shot LVGL timers cancelable and thread-safe: a RAII handle that cancels on
destruction, backed by a mutex-guarded registry — eliminating the systemic use-after-free
behind StartupPage, ScreenSaver, widgets and the WS auth-retry.

## Files (exclusive ownership — do not edit anything else)
- `main/lvgl_timer.h` (modify)
- `main/lvgl_timer.cpp` (modify)
- `tests/unit/test_lvgl_timer.cpp` (new)

## Context
`createOneShot` (`main/lvgl_timer.cpp:204-233`) pushes wrappers into a file-static,
**unsynchronized** `std::vector` (`:162`) and returns `nullptr` (`:232`). Callers therefore
cannot cancel a pending one-shot; their lambdas capture `this` and fire after the owner may be
destroyed (~7 sites in `startup_page.cpp`, plus `screensaver.cpp`, widgets,
`calaos_websocket_manager.cpp` auth-retry). The vector is mutated concurrently: `push_back`
(`:229`) from the dispatcher thread vs the `lv_async_call` erase lambda (`:188-197`) on the
LVGL thread. Additionally the stub getters lie: `isPaused()` always returns `false` (`:144`),
`getPeriod()`/`getRepeatCount()` return `0` (`:151,:158`).

## Approach
1. Introduce `LvglTimer::Handle` (movable, non-copyable): owns a token into a registry;
   `cancel()` method; destructor cancels. Cancellation must be safe from any thread and safe
   against the "already fired / firing right now" races — use a shared control block
   (`std::shared_ptr<std::atomic<bool>> cancelled`) checked inside the timer callback on the
   LVGL thread, plus a `std::mutex`-guarded registry replacing the bare static vector.
2. New API: `static Handle createOneShot(uint32_t delayMs, std::function<void()> cb)`.
   Keep the existing signature as a **deprecated shim** (mark `[[deprecated]]`, discard the
   handle → fire-and-forget preserved) so the ~10 existing call sites compile unchanged;
   their migration belongs to WID-01/UI-01/WSM-01/SP-01.
3. Guard all registry mutations with the mutex; keep actual `lv_timer_*` calls on the LVGL
   thread (the existing `lv_async_call` indirection is the right tool — keep it, fix the
   synchronization around it).
4. Fix or remove the lying getters: either implement `isPaused/getPeriod/getRepeatCount`
   against the real `lv_timer_t`, or delete them if unused (check callers first with grep).
5. Unit tests (host, using FND-02 stubs — stub `lv_timer_create`/`lv_async_call` minimally):
   handle cancel before fire → callback never runs; handle destruction cancels; double-cancel
   is a no-op; cancel-after-fire is a no-op; concurrent create/cancel from two threads doesn't
   crash (ThreadSanitizer-friendly test if stubs allow).

## Out of scope
- Migrating any call site (deprecated shim keeps them compiling; owners: WID-01, UI-01,
  WSM-01, SP-01).
- The periodic-timer RAII path (already owner-managed); only touch it if the registry
  unification requires it.

## Acceptance criteria
- [ ] `createOneShot` returns a `Handle`; destruction/cancel guarantees the callback will not
      run afterwards (or is already running to completion on the LVGL thread — document the
      chosen semantics in the header).
- [ ] Registry access is mutex-guarded; no static unsynchronized vector remains.
- [ ] Old fire-and-forget signature still compiles (deprecated) — repo builds with no call-site
      changes.
- [ ] Unit tests pass; no lying stub getters remain.

## Verification
```bash
./build_linux.sh                                                        # [L]
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build   # [T]
```
