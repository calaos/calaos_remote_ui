# OTA-02: Typed OTA screen commands

- **Priority:** P2
- **Effort:** S
- **Phase:** 4
- **Depends on:** OTA-01, CORE-05
- **Blocks:** none
- **Findings:** m2 — see ../AUDIT.md
- **Status:** backlog

## Objective
Replace the stringly-typed `"show:"/"error:"/"idle"` command channel between OtaManager and
OtaUpdateScreen with a typed command struct, and move the screen onto the shared CORE-05
slot so status and text can never be observed inconsistently.

## Files (exclusive ownership — do not edit anything else)
- `main/ota_update_screen.h` / `.cpp` (modify)
- `main/ota_manager.cpp` (modify — the command-producing sites only)

## Context
(m2) `OtaUpdateScreen` encodes commands as string prefixes in `pendingStatusText_`:
`"show:"+version`, `"error:"+msg`, `"idle"` (`main/ota_update_screen.cpp:244,:274,:309-330`),
parsed with `rfind(...,0)==0` (`:315,:325`) — a legitimate status message starting with
`error:` would be misinterpreted. The state is split across several `std::atomic` fields plus
a mutex-guarded string, loaded separately in `update()` — a status and its text can be
observed torn. The screen is otherwise the codebase's reference implementation of the
snapshot/apply pattern (CORE-05 was modeled on it). The error auto-hide `lv_timer`
(`:181,:288-290`) has a documented double-ownership subtlety — simplify it with the typed
rework if convenient.

## Approach
1. Define `struct OtaUiCommand { enum class Kind { Idle, ShowAvailable, Progress, Error, Done };
   Kind kind; std::string text; int progressPct; ... };` — one struct carries a coherent
   snapshot.
2. Replace the atomics+string combo with a single CORE-05 `UiStateSlot<OtaUiCommand>`:
   producer sites in `ota_manager.cpp` publish complete commands; `update()` consumes one
   coherent snapshot per frame. Latest-wins is correct here (progress coalescing desired).
3. Delete the prefix parsing; the error auto-hide timer becomes a CORE-01-style owned
   handle or a plain owned `lv_timer` with single ownership (destructor-only deletion).
4. Behavior identical: same screens, same 5000 ms error auto-hide (name the constant).

## Out of scope
- OTA logic/lifetime (OTA-01, merged).
- StartupPage OTA gating (SP-02).

## Acceptance criteria
- [ ] No string-prefix command parsing remains (`grep -n 'rfind' main/ota_update_screen.cpp`).
- [ ] Status+text+progress always observed as one coherent snapshot (code review + burst
      test publishing rapid progress).
- [ ] OTA simulation flow on Linux renders identically (available → progress → done, and
      the error path with auto-hide).

## Verification
```bash
./build_linux.sh    # [L]
# Smoke [S]: Linux OTA simulation (linux_ota stub) — full flow + forced error.
```
