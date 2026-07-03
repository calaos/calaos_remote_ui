# WID-01: Widget base helpers + deduplication of 6 widgets

- **Priority:** P1
- **Effort:** L
- **Phase:** 2
- **Depends on:** CORE-01
- **Blocks:** UI-01
- **Findings:** M14, m8, part of C1 (timer call sites) — see ../AUDIT.md
- **Status:** backlog

## Objective
Lift the copy-pasted widget mechanics (container styling, click trampoline, ON/OFF visual
state, `parseIsOn`, server-echo guard, numeric parsing) into shared base/helper code, and
migrate widget timer usage to CORE-01 cancelable handles.

## Files (exclusive ownership — do not edit anything else)
- `main/widgets/*` EXCEPT `clock_widget.h/.cpp` (UI-01 owns those)
- `main/widgets/widget_utils.h` (new, optional home for free helpers)
- `main/calaos_widget.h` / `.cpp` (modify — base-class helpers)
- `main/widget_factory.cpp` (modify if constructor signatures change)

## Context
Six widgets repeat verbatim (M14): the container styling block
(`light_switch_widget.cpp:36-40`, `light_switch_wide_widget.cpp:47-51`,
`scenario_widget.cpp:41-45`, `shutter_widget.cpp:53-57`, `temperature_widget.cpp:31-34`,
`clock_widget.cpp:116-120` — clock is UI-01's); the
`static_cast<X*>(lv_event_get_user_data(e))` click trampoline; the ON/OFF
`updateVisualState` bg/border swap; `parseIsOn` identical in `light_switch_widget.cpp:160-179`
and `light_switch_wide_widget.cpp:165-180`; the hand-rolled `updatingFromServer` reentrancy
flag. Numeric parsing rigor is inconsistent: guarded `stoi` vs `strtod`+endptr
(`temperature_widget.cpp:83-91`, the good example) vs bare `atoi`/`strtol`
(`shutter_common.h:41,:47`). Also (m8): `CalaosWidget::getDisplayName()` returns refs into
members/params (`calaos_widget.cpp:58-74` — subtle lifetime contract), and
`onAppStateChanged` change-detection compares only 4 of 7 fields (`:125-128`).
ScenarioWidget's raw `lv_timer` 400 ms delay (`scenario_widget.cpp:154`) should become a
CORE-01 handle.

## Approach
1. In `CalaosWidget` (or `widget_utils.h`): `applyCardStyle(lv_obj_t*)` for the container
   block; a templated `addClickHandler<T>(obj, T* self, void (T::*fn)())` trampoline;
   `setOnOffVisual(bool)` for the bg/border swap; `parseIsOn(const std::string&)` and a
   single strict `parseNumeric` (strtod/strtol + endptr, modeled on
   `temperature_widget.cpp:83-91`); a small `ServerEchoGuard` RAII for `updatingFromServer`.
2. Migrate the five owned widgets + `shutter_common.h` to the helpers; delete the duplicated
   blocks. Behavior/pixels identical — this is a mechanical dedup.
3. Replace ScenarioWidget's raw `lv_timer` and any widget `LvglTimer::createOneShot`
   fire-and-forget calls with owned CORE-01 `Handle` members (auto-cancel on destruction).
4. Fix `getDisplayName()` to return `std::string` by value (or document + enforce the
   lifetime contract); extend `onAppStateChanged` comparison to all relevant fields or
   comment precisely why the subset is sufficient.
5. Note for UI-01 in the PR description: which helpers clock_widget should adopt.

## Out of scope
- `clock_widget.*` and font/date/TZ dedup (UI-01).
- Widget behavior changes, new widget types, theme colors (SWEEP-01 for inlined greys).
- `sendStateChange`/`g_wsManager` plumbing (APP-01 owns the global's retirement).

## Acceptance criteria
- [ ] The styling block, trampoline, ON/OFF swap, `parseIsOn` and echo-guard each exist
      exactly once (grep proves no per-widget copies remain in owned files).
- [ ] `shutter_common.h` no longer uses bare `atoi`/unchecked `strtol`.
- [ ] No raw `lv_timer_create` or discarded one-shot remains in owned widget files.
- [ ] Simulator smoke: each widget type (light, wide light, scenario, 3 shutters,
      temperature) renders and toggles identically to before.

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: simulator with a server exposing all widget types; toggle each, verify visuals
# and server round-trip (incl. dimmer echo not causing feedback loops).
```
