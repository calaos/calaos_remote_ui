# UI-01: Shared font-selection/date-format util; ScreenSaver & ClockWidget dedup

- **Priority:** P2
- **Effort:** M
- **Phase:** 3
- **Depends on:** CORE-01, WID-01
- **Blocks:** none
- **Findings:** M15 (fonts/date/TZ parts), part of C1 (timer sites) — see ../AUDIT.md
- **Status:** backlog

## Objective
Extract the duplicated font-selection and date-formatting machinery shared by the
screensaver and the clock widget, end the global-TZ `setenv` fight between them, and migrate
their timers to CORE-01 handles.

## Files (exclusive ownership — do not edit anything else)
- `main/ui/format_utils.h` / `.cpp` (new)
- `main/screensaver.h` / `.cpp` (modify)
- `main/widgets/clock_widget.h` / `.cpp` (modify)
- `main/image_sequence_animator.cpp` (modify — timer migration only)

## Context
(M15) The `FontEntry`+`selectFont` "largest font that fits" machinery and the font tables are
duplicated: `main/screensaver.cpp:12-37,:166-182` vs `main/widgets/clock_widget.cpp:12-41,
:94-111`; `formatDate` is duplicated verbatim (`screensaver.cpp:379` vs
`clock_widget.cpp:191`). **TZ fight:** `screensaver.cpp:356-357` and
`clock_widget.cpp:212-213` each call `setenv("TZ", <their own tz>); tzset();` on every 1 s
tick — two modules with different timezones corrupt each other and any other `localtime` in
the process. Also: `ScreenSaver::applyConfig` runs on the dispatcher thread, calls
`setBacklight` outside the display lock (`:339,:345`) and mutates `clock*_` cached strings
racing LVGL-thread readers (`:307-315`); `selectFont` falls back to the largest font even
when nothing fits (`clock_widget.cpp:110` — silent overflow); `static uint32_t lastLogMs`
inside `update()` is shared across instances (`screensaver.cpp:213`). WID-01 (merged) left a
note on which base helpers clock_widget should adopt — apply it.

## Approach
1. `main/ui/format_utils.h/.cpp`: one `selectFont(span<FontEntry>, availableW, availableH,
   text)` (returns nullopt/smallest when nothing fits — log once, don't overflow), one
   shared font table, one `formatDate`/`formatTime`.
2. Thread-safe timezone conversion: `localtimeInZone(time_t, const std::string& posixTz)`
   using a single global mutex around the `setenv/tzset/localtime_r/restore` sequence (the
   portable option given ESP-IDF newlib — document why; a lock makes the fight deterministic
   and contained in one place). Both consumers use it; direct `setenv("TZ",...)` calls are
   deleted from both files.
3. ScreenSaver `applyConfig` thread safety: cache the incoming config into a snapshot
   applied on the LVGL thread (mutex-guarded copy — CORE-05's `UiStateSlot` may be used if
   convenient, but a local mutex is acceptable; full subscriber migration is STORE-01/SP-01
   territory, keep this minimal). Move the `setBacklight` calls inside the applied-on-LVGL
   path or document why backlight is thread-safe.
4. Migrate remaining `LvglTimer::createOneShot` fire-and-forget calls in screensaver and
   image_sequence_animator to owned CORE-01 `Handle`s; fix the shared `static lastLogMs`.
5. clock_widget adopts the WID-01 base helpers (card style, trampoline) where applicable.

## Out of scope
- Other widgets (WID-01, merged).
- NotificationToast/about page subscribers (STORE-01).
- ImageSequenceAnimator API pruning and its misleading `threadSafe` flag (SWEEP-01).

## Acceptance criteria
- [ ] One copy of font tables/selectFont/formatDate (grep shows none left in the two
      consumers).
- [ ] `grep -n 'setenv' main/screensaver.cpp main/widgets/clock_widget.cpp` → none; a
      screensaver and a clock widget configured with different timezones display both
      correctly simultaneously (simulator).
- [ ] No fire-and-forget one-shot timers remain in owned files.
- [ ] Screensaver engage/disengage and clock rendering unchanged visually.

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: simulator with clock widget tz=America/New_York and screensaver tz=Europe/Paris;
# both correct across a minute rollover; screensaver in/out repeatedly.
```
