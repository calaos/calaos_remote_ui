# FND-03: PR CI pipeline + board matrix fixes

- **Priority:** P1
- **Effort:** S
- **Phase:** 1
- **Depends on:** FND-01, FND-02
- **Blocks:** none
- **Findings:** M18 (see ../AUDIT.md)
- **Status:** backlog

## Objective
Make every pull request build the Linux target and run the unit tests; fix the board-matrix
inconsistencies in the existing workflows.

## Files (exclusive ownership — do not edit anything else)
- `.github/workflows/ci.yml` (new)
- `.github/workflows/build-firmware.yml` (modify)
- `.github/workflows/build-docker-images.yml` (modify, if needed for dropdown consistency)
- `boards/ci-boards.json` (modify)

## Context
Both existing workflows trigger only on push to `main` or manual dispatch — no PR validation
exists (M18). `boards/ci-boards.json` lists 5 boards but the `workflow_dispatch` dropdown in
`build-firmware.yml:10-14` offers only `all` + `waveshare-86-panel`. The `luckfox-86-panel`
entry lacks `bin_path`/`flash_package` keys, yet the build job uploads
`path: ${{ matrix.bin_path }}` with `if-no-files-found: error` (`:90-95`) — that matrix leg is
guaranteed to fail at the artifact step. See `DOC-CI.md` for the existing CI design.

## Approach
1. New `ci.yml` triggered on `pull_request` (and `push` to `main`): ubuntu runner, install SDL2
   deps, run `./build_linux.sh`, then build+run the `tests/` project from FND-02. Keep it
   fast (<10 min); no ESP32 build on PRs for now (document as future work).
2. Fix `build-firmware.yml`: align the `workflow_dispatch` board dropdown with
   `ci-boards.json` (all 5 boards + `all`).
3. Fix the luckfox leg: either add correct `bin_path`/`flash_package` values to
   `ci-boards.json`, or make the upload step conditional (`if: matrix.bin_path != ''` /
   `if-no-files-found: warn`) with a comment explaining why.
4. Validate YAML with `actionlint` if available, else careful review.

## Out of scope
- Running ESP32/IDF builds on PRs (image pulls are heavy; note as follow-up in BOARD.md).
- Modifying the Dockerfiles or build.sh scripts (BRD-01 owns boards/ build files).
- Test content (FND-02).

## Acceptance criteria
- [ ] A test PR triggers `ci.yml`; Linux build + ctest run and pass.
- [ ] Board dropdown lists all boards from `ci-boards.json`.
- [ ] The luckfox matrix leg no longer errors at artifact upload (fixed path or conditional).

## Verification
```bash
# YAML sanity:
actionlint .github/workflows/*.yml || python3 -c "import yaml,glob; [yaml.safe_load(open(f)) for f in glob.glob('.github/workflows/*.yml')]"
# Then open a draft PR and observe the ci.yml run.
```
