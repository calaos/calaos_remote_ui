# AGENTS.md - Calaos Remote UI

ESP-IDF / LVGL firmware plus a Linux simulator. CI and test details are in [DOC-CI.md](DOC-CI.md). Cross-repo notes (wire format, IODoc, workflow pitfalls): [calaos_base docs/18_stack_notes.md](https://github.com/calaos/calaos_base/blob/master/docs/18_stack_notes.md) (in French).

## Build and test

Build and test only in the dev container (`.devcontainer/`; doc-website tooling refers to its image as `calaos-remote-ui-dev:local`) or in the board image CI uses; nothing is installed on the host. Host tests (`DOC-CI.md`, "Host tests"):

```sh
cmake -S . -B build_linux -DBUILD_LINUX=ON -DBOARD=linux-dev -DBUILD_HOST_TESTS=ON
cmake --build build_linux -j"$(nproc)"
ctest --test-dir build_linux --output-on-failure
```

Firmware (`waveshare-86-panel`) is built with `idf.py` in its board image (`DOC-CI.md`, `.github/workflows/pull-request.yml:100-110`). Docker leaves root-owned files in the worktree; do not build under `/tmp` (RAM tmpfs).

## Pitfalls

- **Climate modes and range come from the server.** The mode list is `capabilities.modes` (`main/widgets/climate_logic.cpp:121`) and the range is shown only with `target_temp_range` (`climate_logic.cpp:122,199`). Do not hardcode modes.
- **Fonts exist only at generated sizes.** Roboto is converted by `fonts/generate.sh` (sizes listed in `FONT_CONFIGS`) into `main/fonts/`; LVGL has no scaling fallback, so using another size needs a new entry and a regeneration. Large sizes are ASCII-only (threshold in the script). `main/fonts/` also holds size-40 files that `FONT_CONFIGS` no longer lists: regenerating would not reproduce them.
- **Icons are PNGs; some glyphs are drawn.** Icons live in `main/images/` (climate ones rendered by `scripts/render-climate-icons.sh`). Chevrons, crosses and plus/minus are drawn with `lv_line` (`main/widgets/climate_ui.cpp:147-151`, `climate_detail.cpp`) because the generated fonts lack those glyphs: do not use them as text.
