# FND-01: Enable -Wall/-Wextra, honor build type, fix build script

- **Priority:** P1
- **Effort:** S
- **Phase:** 0
- **Depends on:** none
- **Blocks:** FND-03, NET-01, BRD-01
- **Findings:** M17 (see ../AUDIT.md)
- **Status:** ready

## Objective
Turn on compiler warnings for both platforms, make the Linux build respect the requested
build type, and clean up `build_linux.sh` — establishing the "warning ratchet" baseline used
by every later ticket.

## Files (exclusive ownership — do not edit anything else)
- `CMakeLists.txt` (modify)
- `build_linux.sh` (modify)

## Context
No warning flags exist anywhere: the Linux branch sets only `-g` (`CMakeLists.txt:160`).
`build_linux.sh:29` passes `-DCMAKE_BUILD_TYPE=Release` but `CMakeLists.txt:159` hard-codes
`set(CMAKE_BUILD_TYPE Debug)`, silently overriding it — the Linux binary is always Debug.
The `if [ $? -eq 0 ]` success check at `build_linux.sh:36` is dead code under `set -e`.
Minor CMake cruft in the same file: `find_package(Python3)` called twice (`:102,:152`); dead
`LV_USE_LINUX_FBDEV`/`LV_USE_LINUX_EVDEV` vars (`:166-172`) shadowed by the `LV_USE_FBDEV`/
`LV_USE_EVDEV` names the template actually consumes.

## Approach
1. Add `-Wall -Wextra` (NOT `-Werror`) to the compile options for project sources on both the
   Linux branch and the ESP-IDF component build. Scope the flags to project code only
   (`main/`, `hal/`, `network/`, `flux/`) — do not force them onto vendored `components/`
   (lvgl, mongoose, nlohmann) or `smooth_ui_toolkit`, which would drown the signal.
2. Replace the hard-coded `set(CMAKE_BUILD_TYPE Debug)` with a guarded default:
   only set `Debug` if `CMAKE_BUILD_TYPE` is empty.
3. In `build_linux.sh`: remove the dead `$?` check; keep `set -e` semantics; make sure the
   Release flag propagates (verify with `grep CMAKE_BUILD_TYPE build/CMakeCache.txt`).
4. Remove the duplicate `find_package(Python3)` and the dead `LV_USE_LINUX_*` variables.
5. Record the warning baseline: build Linux Debug, count warnings in project files
   (`./build_linux.sh 2>&1 | grep -c 'warning:'` filtered to main/hal/network/flux paths), and
   write the number into a new "Warning baseline" line at the bottom of this ticket file.

## Out of scope
- Fixing any warning the new flags reveal (later tickets own their files' warnings).
- `-Werror`, sanitizers, clang-tidy.
- Test harness (FND-02), CI (FND-03).

## Acceptance criteria
- [ ] `./build_linux.sh` succeeds; `CMakeCache.txt` shows `CMAKE_BUILD_TYPE=Release` when the
      script requests Release.
- [ ] `-Wall -Wextra` visible in compile commands for a file under `main/` on both platforms,
      and absent for `components/lvgl` sources.
- [ ] `idf.py build` (any Waveshare board) still succeeds.
- [ ] Warning baseline count recorded in this file.

## Verification
```bash
./build_linux.sh                          # [L]
grep CMAKE_BUILD_TYPE build/CMakeCache.txt
# ESP32 (docker per boards/<board>/build.sh, or local idf.py):   [E]
idf.py build
```
