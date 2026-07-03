# DEP-01: De-vendor dependencies (LVGL, nlohmann-json, mongoose) — OPTIONAL

- **Priority:** P3 (optional — schedule only when the core backlog is done)
- **Effort:** L
- **Phase:** 5+ (after SWEEP-01; no other ticket depends on it)
- **Depends on:** SWEEP-01
- **Blocks:** none
- **Findings:** m11 — see ../AUDIT.md
- **Status:** backlog

## Objective
Replace the fully-vendored dependency copies with pinned submodules/FetchContent + a
documented patch mechanism, shrinking the repo and making upstream updates tractable.

## Files (exclusive ownership — do not edit anything else)
- `components/**` (lvgl, nlohmann-json, mongoose, smooth_ui_toolkit_esp32 wrapper)
- `.gitmodules`, `CMakeLists.txt`, `cmake/**` (integration), `patches/**` (new)
- `.github/workflows/*` (checkout submodules), `boards/Dockerfile` (if caching needs it)

## Context
(m11) `components/lvgl` is a 5,418-file vendored LVGL 9.5.0 carrying a **local PPA patch**
(see `sdkconfig.defaults.esp32p4:50-56`) — upstream updates require re-discovering and
re-applying it. `components/nlohmann-json` vendors 1,152 files (full test suite + docs) when
only `single_include` is consumed. `components/mongoose` (7.8.x) is also fully vendored.
`smooth_ui_toolkit` exists twice: top-level (Linux `add_subdirectory`) and a one-file
`components/smooth_ui_toolkit_esp32` wrapper — two entry points to one library (keep the
wrapper, document it). High git churn, long-term payoff; deliberately last and optional.

## Approach
1. **Identify the LVGL local patch first**: diff vendored tree vs upstream v9.5.0 tag;
   extract to `patches/lvgl/*.patch` with a README explaining each hunk. This step is
   valuable even if the ticket stops here — do it first and commit it separately.
2. LVGL → git submodule pinned to v9.5.0 + patch application at configure time
   (`PATCH_COMMAND`/script, idempotent). Verify the ESP-IDF component manager isn't a better
   fit for the ESP32 side (`idf_component.yml`) — choose one mechanism for both platforms if
   possible, document the trade-off.
3. nlohmann-json → FetchContent or a single vendored `json.hpp` (the single-header IS the
   supported distribution — smallest diff, no network-at-build dependency; recommended).
4. mongoose → submodule pinned at the current 7.8.x commit (its 2-file distribution also
   permits a minimal vendor — decide by whether local modifications exist; diff first).
5. CI: `submodules: recursive` in checkouts; Docker build caching still works; offline build
   documented (mirror/`GIT_SUBMODULE` strategy).
6. Full regression: Linux + all ESP32 boards build; binary size unchanged (LVGL config
   identical); simulator smoke.

## Out of scope
- Upgrading any dependency version (pin exactly what is vendored today; upgrades are
  separate future tickets).
- smooth_ui_toolkit restructuring beyond documentation.

## Acceptance criteria
- [ ] Repo tracked-file count drops by thousands; `git clone --recursive && build` works on
      a clean machine (documented).
- [ ] The LVGL local patch is explicit, versioned, and auto-applied; upstream bump procedure
      documented in `patches/README.md`.
- [ ] All boards + Linux build bit-comparable feature-wise (same sdkconfig/lv_conf results);
      CI green.

## Verification
```bash
git clone --recursive <repo> /tmp/fresh && cd /tmp/fresh && ./build_linux.sh   # [L]
idf.py build (all boards via CI matrix)                                        # [E]
```
