# PROTO-01: Protocol parse robustness, per-widget error containment

- **Priority:** P0
- **Effort:** M
- **Phase:** 3
- **Depends on:** CORE-06, FND-02
- **Blocks:** STORE-01
- **Findings:** M8 (protocol part) — see ../AUDIT.md
- **Status:** backlog

## Objective
Make pages-config parsing tolerant: a malformed widget degrades to an error placeholder
instead of blanking the entire UI, and no numeric-parse exception can escape the protocol
layer.

## Files (exclusive ownership — do not edit anything else)
- `main/calaos_protocol.h` / `.cpp` (modify)
- `main/calaos_page.cpp` (modify — error containment at the consumer)
- `tests/unit/test_calaos_protocol.cpp` (new)

## Context
`main/calaos_protocol.cpp:68-106` parses `x/y/w/h/width/height` with `std::stoi` inside
`try { } catch (const json::exception&)` (`:16,:160`) — `std::invalid_argument`/
`std::out_of_range` are NOT `json::exception`, so a widget with `x:"abc"` escapes and is
caught two frames up at `main/calaos_page.cpp:391` (`catch(std::exception&)`), which drops
the **entire page set**: one malformed widget coordinate blanks the whole UI (M8). Also:
`PagesConfig` default-member-initializers call `HAL::getInstance()`
(`main/calaos_protocol.h:88-89`) — hidden global side effect on every default construction
including the parse-failure return. The existing degrade path to `WidgetError`
(`widget_factory.cpp:85-88`, for unsupported types) is the model for containment — reuse it,
do not modify widget_factory.
FND-02 note: if protocol sources needed HAL stubs deferred from the harness ticket, extend
`tests/stubs/` here (stub additions in `tests/` are allowed — glob discovery, no shared edits).

## Approach
1. Replace all `std::stoi` in the protocol layer with CORE-06 `jsonu::asInt`/`intFieldOr`
   (tolerant, exception-free). Per-widget parse becomes
   `std::optional<WidgetConfig> parseWidget(const json&)` — a malformed widget yields
   nullopt + one WARN log naming id/field, and parsing continues with the remaining widgets.
2. In `PagesConfig::fromJson`: collect malformed widgets as explicit error entries (type
   `WidgetError` with the failure reason as label) rather than dropping them, so the user
   sees a placeholder where the widget would be — consistent with the unsupported-type path.
3. In `calaos_page.cpp`: the `catch` at `:391` becomes last-resort only; a partially-valid
   config renders its valid pages/widgets. Keep the `lastConfigJson` string-compare recreate
   behavior as-is (known coarse, out of scope).
4. Remove the `HAL::getInstance()` calls from `PagesConfig` default member initializers —
   pass screen dimensions in explicitly (or default to 0 and resolve at creation site in
   calaos_page.cpp where HAL is legitimately available).
5. Unit tests: full valid config; one widget with `x:"abc"` → other widgets survive + error
   placeholder present; missing fields; wrong-typed page list; empty config; out-of-range
   coords (bounds check at `calaos_page.cpp:309-324` behavior pinned).

## Out of scope
- `handleConfigUpdate` in the WS manager (WSM-01).
- Widget rendering/factory changes (`widget_factory.cpp` is read-only here).
- Page diffing/incremental rebuild (future work; note in BOARD.md if desired).

## Acceptance criteria
- [ ] Feeding the app a pages config with one corrupt widget renders all other widgets plus
      an error placeholder (simulator-verified).
- [ ] No `std::stoi`/unguarded numeric parse remains in `calaos_protocol.cpp` (grep).
- [ ] `PagesConfig` construction has no HAL side effect (host tests construct it freely).
- [ ] Unit tests pass.

## Verification
```bash
cmake -S tests -B tests/build && cmake --build tests/build -j && ctest --test-dir tests/build   # [T]
./build_linux.sh    # [L]
# Smoke [S]: simulator against a server; then a mock/modified payload with one bad widget.
```
