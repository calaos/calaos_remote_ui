# FND-02: Standalone host unit-test harness

- **Priority:** P1
- **Effort:** M
- **Phase:** 0
- **Depends on:** none
- **Blocks:** FND-03, CORE-01, CORE-06, DISC-01, PROTO-01
- **Findings:** M18 (see ../AUDIT.md)
- **Status:** ready

## Objective
Create a self-contained host-side unit-test project under `tests/` so that later tickets can
add tests by dropping a file, without touching any shared build file.

## Files (exclusive ownership — do not edit anything else)
- `tests/**` (all new: `tests/CMakeLists.txt`, framework, stubs, unit tests)

## Context
There are zero tests in the repo (M18). Pure-logic code that badly wants tests: protocol
parsing (`main/calaos_protocol.cpp` — see M8), provisioning config CRC/JSON
(`hal/calaos_config/calaos_config.cpp`), HMAC construction (`main/hmac_authenticator.cpp`),
discovery announce parsing (`main/calaos_discovery.cpp`). These sources include LVGL/ESP-IDF
headers indirectly (e.g. `HAL::getInstance()` inside `calaos_protocol.h:88-89` — see M8), so
the harness needs minimal stubs.

## Approach
1. Create `tests/CMakeLists.txt` as a **standalone** CMake project (NOT wired into the root
   `CMakeLists.txt` — the root file is owned by FND-01 this phase). C++17, host toolchain.
2. Vendor or FetchContent a single-header framework (doctest recommended: one header, fast).
3. Create `tests/stubs/` with the minimum shims needed to compile the target sources on host:
   `esp_log.h` stub mapping `ESP_LOGx` to `printf`, plus whatever small HAL/LVGL surface the
   chosen sources pull in. Keep stubs header-only where possible. If `calaos_protocol.cpp`
   can't compile without heavy HAL stubbing, start with `hmac_authenticator.cpp` +
   `hal/calaos_config/*` and leave a stub extension note for PROTO-01.
4. Test discovery by glob: `file(GLOB TEST_SOURCES unit/test_*.cpp)` so future tickets add
   `tests/unit/test_<x>.cpp` with zero CMake edits. Register with CTest.
5. Seed with real smoke tests: (a) HMAC vector test against a known input/key/output;
   (b) config CRC round-trip; (c) one trivial always-true placeholder documenting the layout.
6. Add `tests/README.md`: how to build/run, how to add a test, stub philosophy.

## Out of scope
- CI wiring (FND-03).
- Testing LVGL/UI code, threading code, or network I/O.
- Modifying any production source to make it testable (if a source can't compile on host,
  document it and pick another seed target).

## Acceptance criteria
- [ ] `cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build`
      passes on a clean checkout.
- [ ] Adding a new `tests/unit/test_dummy.cpp` requires no CMake edit (glob picks it up on
      reconfigure).
- [ ] At least one real assertion against production code (HMAC or config CRC).
- [ ] No production file modified (`git status` shows only `tests/`).

## Verification
```bash
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build --output-on-failure   # [T]
git status --short   # only tests/ files
```
