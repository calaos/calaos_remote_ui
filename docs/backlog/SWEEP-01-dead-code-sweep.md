# SWEEP-01: Dead code, cruft, and magic-number sweep

- **Priority:** P2
- **Effort:** M
- **Phase:** 5
- **Depends on:** all Phase 4 tickets merged
- **Blocks:** DEP-01
- **Findings:** m3, m4, m6, m7, m9, m10 (naming), m12 — see ../AUDIT.md
- **Status:** backlog

## Objective
Final hygiene pass: delete the remaining dead code, replace magic-string error signalling,
name the remaining magic numbers, and normalize the leftover cruft — everywhere except the
files SP-02 owns.

## Files (exclusive ownership — do not edit anything else)
Everything needed by the inventory below EXCEPT `main/startup_page.*` and `main/startup/**`
(owned by SP-02, running concurrently). Re-run the grep inventory at start; expected set:
- `main/provisioning_crypto.h` / `.cpp`, `main/provisioning_manager.cpp` (ERROR-string protocol)
- `main/hmac_authenticator.h` / `.cpp` (dead `hexToBytes` with unguarded `std::stoi`)
- `main/image_sequence_animator.h` / `.cpp` (misleading `threadSafe` flag, unused API)
- `main/calaos_page.h` (comment cruft), `main/theme.h` / `.cpp` + inlined-grey call sites
  (`main/calaos_page.cpp`, `main/about_page.cpp`, `main/widgets/widget_error.cpp`)
- `hal/linux/linux_hal_display.cpp` (dead `linuxFbFlush`, ignored `vinfo`)
- `hal/esp32/esp32_hal_display.cpp` (magic DPI/buffer constants → board_config-derived or named)
- `flux/app_dispatcher.cpp` (identical `#ifdef` branches; busy-poll teardown comment/fix)
- `main/calaos_discovery.cpp` (env-backdoor isolation if DISC-01 deferred it)

## Context
Accumulated minors (AUDIT.md): (m4) crypto signals errors via magic strings
`ERROR0/1/2` checked with `starts_with("ERROR")` (`provisioning_manager.cpp:138,:272`,
`provisioning_crypto.cpp:156,:170`) → replace with `std::optional`/status enum; redundant
base32 truncation branch (`provisioning_crypto.cpp:122-140`). (m3) `hexToBytes` is dead and
contains an unguarded `std::stoi` (`hmac_authenticator.cpp:99-106`). (m9)
`ImageSequenceAnimator` advertises `config_.threadSafe` without any locking
(`image_sequence_animator.cpp:107-135`) and carries ~200 lines of never-used API
(`pause/reset/showStatic/setFrames/createLoop/createPingPong`) — delete the flag and the
unused surface after a grep-confirmed caller inventory. (m7) `// CHANGED:`/`// NEW:` diff
cruft (`calaos_page.h:21-39`); inactive-grey colors `0x666666/0x888888` inlined at 5+ sites
instead of `theme.cpp`. (m6) remaining magic numbers in owned files → named `constexpr`
(display DPI 180, buffer `HRes*200`, stack sizes — move to board config or named constants;
do NOT re-tune values). (m12) flux: collapse the identical `#ifdef` include branches; either
fix the busy-poll teardown with a proper join primitive or document why polling is
acceptable. (m10) member-naming drift ESP32 vs Linux HAL (`network` vs `network_`) —
normalize ONLY if the diff stays mechanical; skip if it collides with open work.

## Approach
1. Re-inventory first (grep TODO/FIXME/CHANGED/NEW, `starts_with("ERROR")`, dead symbols via
   `-Wunused` + LSP references) and paste the final file list into this ticket before
   starting — anything colliding with SP-02's files is dropped or deferred.
2. Delete/replace in small commits grouped by module; every deletion backed by a
   zero-references check.
3. Theme: add the inactive/label greys to `theme.cpp` and swap the inlined literals.
4. Keep the diff strictly non-behavioral (constants keep their values; only names/structure
   change). Anything that turns out to be behavioral gets split out as a new ticket.

## Out of scope
- `main/startup_page.*`, `main/startup/**` (SP-02).
- Dependency de-vendoring (DEP-01).
- Any value tuning or feature change.

## Acceptance criteria
- [ ] `grep -rn 'starts_with("ERROR")\|ERROR0\|ERROR1\|ERROR2' main/` → none; crypto errors
      are typed.
- [ ] `hexToBytes`, `linuxFbFlush`, unused animator API, `CHANGED:/NEW:` comments: gone.
- [ ] No inlined `0x666666`-style theme colors outside `theme.cpp` (grep).
- [ ] Both builds pass; warning count strictly decreases (ratchet bonus).
- [ ] Simulator smoke: app runs, pages render, screensaver + provisioning unaffected.

## Verification
```bash
./build_linux.sh    # [L]
idf.py build        # [E] — HAL/display files touched
# Smoke [S]: general navigation pass on the simulator.
```
