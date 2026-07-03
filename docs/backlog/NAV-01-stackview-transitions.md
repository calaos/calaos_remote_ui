# NAV-01: StackView — queued transitions + animation dedup

- **Priority:** P1
- **Effort:** M
- **Phase:** 2
- **Depends on:** none
- **Blocks:** STORE-01
- **Findings:** M7 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Stop silently dropping page transitions requested during an animation (queue them instead),
collapse the four near-identical animation setup functions, and fix the dangling raw
`PageBase*` captures in animation callbacks.

## Files (exclusive ownership — do not edit anything else)
- `main/stack_view.h` / `.cpp` (modify)

## Context
`push`/`pop` return early with no log and no queue while `animating`
(`main/stack_view.cpp:23-26,:51-54`) — a `push(CalaosPage)` issued during a running animation
is silently dropped; combined with StartupPage's `size()<=1` guards, a lost push can strand
the app on the startup screen forever (M7). `setupSlideVerticalPush/Pop` and
`setupSlideHorizontalPush/Pop` (`:176-338`, ~160 lines) differ only in axis (`set_y` vs
`set_x`) and sign. Animation `onUpdate` lambdas capture raw `PageBase*`
(`:191,:232,:273,:314`); `clear()` (`:70`) drops all pages ignoring in-flight animations —
`animatingPage`/`previousPage` can dangle.

## Approach
1. Add a small pending-transition queue (deque of {op, page, animation type}); on animation
   completion (`onAnimationComplete`, `:362` area) pop and execute the next queued
   transition. Log at INFO when queueing. Bound the queue (e.g. collapse consecutive
   redundant ops) to avoid pathological buildup.
2. Merge the four setup functions into one parameterized
   `setupSlideAnimation(Axis, Direction, PageBase* incoming, PageBase* outgoing)`.
3. Lifetime: cancel/complete any running animation before a page is removed or `clear()`ed
   (stop the smooth_ui_toolkit animations and detach their callbacks), so no lambda can
   dereference a freed `PageBase*`. Make `clear()` safe mid-animation.
4. Keep the public API (`push`/`pop`/`clear`/`size`) source-compatible — StartupPage/AppMain
   are not in this ticket's file set.

## Out of scope
- Callers (`main/startup_page.cpp`, `main/app_main.cpp`).
- New animation types or timing changes.
- PageBase itself.

## Acceptance criteria
- [ ] A push issued mid-animation executes after the current animation (observable in
      simulator: spam navigation, every requested page eventually shows; nothing stranded).
- [ ] One parameterized animation-setup function; the four variants are gone (file shrinks
      by ~120+ lines).
- [ ] `clear()` and page removal mid-animation cause no crash under rapid navigation
      (ASAN-clean if available).
- [ ] No public API change (repo builds with only this file pair modified).

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: simulator — rapid startup→main page transitions, screensaver in/out, about page
# open/close spam; verify no dropped screens and no crash.
```
