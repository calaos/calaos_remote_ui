# BRD-01: Boards/build dedup + relay table single-source

- **Priority:** P2
- **Effort:** M
- **Phase:** 2
- **Depends on:** FND-01
- **Blocks:** HALE-02
- **Findings:** M16 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Deduplicate the per-board build files (Dockerfiles, build.sh), single-source the relay GPIO
table with a board-defined count (lifting the hard cap of 2), and make placeholder pin
configs fail loudly.

## Files (exclusive ownership — do not edit anything else)
- `boards/**` (Dockerfiles, build.sh, *.cmake, ci-boards.json only if paths change)
- `cmake/board_config.h.in` (modify)
- `hal/esp32/esp32_hal_relay.h` / `.cpp` (modify)
- `hal/linux/linux_hal_relay.h` / `.cpp` (modify)

## Context
The 4 Waveshare board Dockerfiles are byte-identical; the `build.sh` scripts differ only in a
comment and the hard-coded `boards/<name>/partitions.csv` path; the `.cmake` files differ
only in name/resolution/paths (M16). `cmake/board_config.h.in:40-41` emits exactly
`BOARD_RELAY_1_GPIO`/`BOARD_RELAY_2_GPIO`; the array `{BOARD_RELAY_1_GPIO, BOARD_RELAY_2_GPIO}`
is re-declared at `hal/esp32/esp32_hal_relay.cpp:14,:52` and
`hal/linux/linux_hal_relay.cpp:15,:60,:89` (5 sites); `BOARD_RELAY_COUNT > 2` would silently
index out of bounds. `boards/luckfox-86-panel.cmake:24-25` ships placeholder `GPIO 0 # TODO` —
GPIO 0 is a real pin. Boards with 0 relays must still define both GPIOs
(`waveshare-touchlcd-7.cmake:24-25`). The BSP symbol-collision workaround
(`EXCLUDE_COMPONENTS` + include-order hack, `esp32_hal_display.cpp:13-23`) is fragile — do
NOT touch it here, but document it in `boards/README.md`.

## Approach
1. Single `boards/Dockerfile` + single parameterized `boards/build.sh` taking the board name
   (deriving partition/sdkconfig paths from it); per-board directories keep only genuinely
   board-specific files (partitions.csv, sdkconfig.defaults, .cmake). Thin wrapper scripts
   may remain for CI compatibility if `.github/workflows` references them (do not edit
   workflows here — if a wrapper is needed, keep the old entry points calling the shared
   script).
2. Relay table generation: in each board `.cmake`, define `BOARD_RELAY_GPIOS` as a list;
   emit a single `BOARD_RELAY_GPIO_TABLE {13, 14}` (initializer-list string) +
   `BOARD_RELAY_COUNT` from `board_config.h.in`. CMake-side validation:
   `list(LENGTH ...)` must equal `BOARD_RELAY_COUNT`, else `message(FATAL_ERROR)`.
3. Both relay HALs consume the generated table once
   (`static constexpr int kRelayGpios[] = BOARD_RELAY_GPIO_TABLE;`) — delete the 5 duplicated
   array declarations; loops use `BOARD_RELAY_COUNT`/`std::size(kRelayGpios)`.
4. Luckfox placeholders: set `BOARD_RELAY_COUNT 0` + empty list until real pins are known
   (that board's relay feature is currently mis-wired to GPIO 0); leave a tracked TODO in the
   board cmake.
5. `boards/README.md` (new, inside owned dir): how to add a board — checklist, the BSP
   collision trap, relay list syntax.

## Out of scope
- Relay init/deinit lifecycle (HALE-02 adds the deinit hook; coordinate — this ticket keeps
  existing init behavior, only the table source changes).
- CI workflow edits (FND-03 done; if wrapper paths change, keep compatibility instead).
- The BSP include-order refactor itself.

## Acceptance criteria
- [ ] One Dockerfile and one parameterized build script; per-board dirs contain only
      board-specific config (`diff` of any two Waveshare dirs shows only real differences).
- [ ] Relay GPIO array defined in exactly one generated location; a 3-relay board builds
      correctly; a count/list mismatch fails at CMake configure time.
- [ ] Luckfox no longer silently targets GPIO 0; relay disabled there.
- [ ] `idf.py build` passes for ≥2 different Waveshare boards + Linux build unaffected.

## Verification
```bash
./build_linux.sh                              # [L]
# ESP32 for two boards (docker or local):      [E]
BOARD=waveshare-86-panel ./boards/build.sh
BOARD=waveshare-touchlcd-7 ./boards/build.sh
```
