# CORE-06: Safe JSON access helpers

- **Priority:** P0
- **Effort:** S
- **Phase:** 1
- **Depends on:** FND-02
- **Blocks:** CFG-01, WSM-01, PROTO-01
- **Findings:** M8 foundation (see ../AUDIT.md)
- **Status:** backlog

## Objective
Provide small, unit-tested helpers for tolerant extraction of typed values from untrusted
server JSON, so one malformed field never again throws past a too-narrow catch and destroys
a whole config/page update.

## Files (exclusive ownership — do not edit anything else)
- `main/json_utils.h` (new, header-only)
- `tests/unit/test_json_utils.cpp` (new)

## Context
Two failure patterns recur (M8): (a) `std::stoi` on JSON string fields inside
`catch (const json::exception&)` — `std::invalid_argument` escapes
(`main/calaos_protocol.cpp:68-106`); (b) `.get<int>()` on fields whose wire type varies
between int and string (`main/calaos_websocket_manager.cpp:795,:809,:822` — the server is
known to send ints as strings, empty strings, and has the `"brigtness"` typo). Consumers
migrate in PROTO-01/WSM-01/CFG-01 — this ticket only builds the tools.

## Approach
1. Header-only `main/json_utils.h` (namespace `jsonu`), using the vendored nlohmann:
   - `template<typename T> T getOr(const json& j, const char* key, T def)` — missing key,
     null, or wrong type → `def`; never throws.
   - `std::optional<int> asInt(const json& v)` — accepts number, numeric string
     (full-string strtol validation, no exceptions), bool→0/1; empty string/garbage → nullopt.
     Same for `asDouble`, `asBool` (accepts `"true"/"false"/1/0`), `asString`.
   - `int intFieldOr(const json& j, const char* key, int def)` convenience combining the two.
   - `template<typename F> bool tryParse(const char* tag, F&& fn)` — runs `fn`, catches
     **`std::exception`** (not just `json::exception`), logs via `ESP_LOGE(tag, ...)`,
     returns success.
2. No exceptions escape any helper; document that guarantee in the header.
3. Unit tests covering the real-world malformed payloads from AUDIT.md M8: int-as-string,
   string-as-int, empty string ("" → nullopt, the bug fixed in commit e7264fb), null, missing
   key, wrong-type object/array, out-of-range number, `stoi`-would-throw garbage.

## Out of scope
- Migrating any consumer (PROTO-01, WSM-01, CFG-01).
- Wrapping/validating whole document schemas.

## Acceptance criteria
- [ ] All helpers are noexcept-in-effect (no exception escapes, verified by tests feeding
      hostile values).
- [ ] Tests enumerate and pass the malformed-payload matrix above.
- [ ] Header compiles standalone on both platforms (include it from one TU or a test only —
      no production call sites yet).

## Verification
```bash
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build   # [T]
./build_linux.sh    # [L]
```
