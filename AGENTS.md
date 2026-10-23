# AGENTS.md - Calaos Remote UI

Firmware of the Calaos wall panels: LVGL UI in C++20 on ESP32-P4 boards (ESP-IDF v5.5.5), and the same
sources built as a Linux desktop application (simulator, host tests). It talks to `calaos_server`
over an HMAC-authenticated WebSocket and supports OTA. Human overview: [README.md](README.md).

Targets are ESP32-P4, not ESP32-S3. Do not add S3-specific assumptions.

## Where things live

| Topic | Place |
|---|---|
| CI workflows, versioning, `.deb` and flash package formats, adding a board to CI | [DOC-CI.md](DOC-CI.md) |
| Why ESP-IDF is patched (PPA hang, MIPI-DSI clock lane) | [patches/README.md](patches/README.md) |
| Technical-debt audit and refactoring tickets (planned work, not a description of the code) | `docs/AUDIT.md`, `docs/BOARD.md`, `docs/backlog/` |
| Server side of the protocol (in French) | calaos_base `docs/07_remoteui.md`, `docs/18_stack_notes.md` |

There is no architecture document in the repository. Read the code; the entry points are
`main/main.cpp` -> `AppMain` (`main/app_main.cpp`) -> `StartupPage` (`main/startup_page.cpp`).
`docs/AUDIT.md` cites line numbers that drift and ticket statuses in `docs/BOARD.md` lag the code:
verify before relying on them.

## Build and verify

Build in the ESP-IDF board image or the dev container (`.devcontainer/`), not with a host-installed
toolchain. Do not assume a host ESP-IDF.

Host tests and Linux simulator (what the PR CI runs, `.github/workflows/pull-request.yml`):

```sh
cmake -S . -B build_linux -DBUILD_LINUX=ON -DBOARD=linux-dev -DBUILD_HOST_TESTS=ON
cmake --build build_linux -j"$(nproc)"
ctest --test-dir build_linux --output-on-failure
```

`./build_linux.sh [board]` builds only the simulator (`build_linux/calaos-remote-ui`; run it with
`--help` for the display and input backend options). `CLIMATE_SHOTS_DIR=<dir> build_linux/tests/widget_tests` writes PNGs of the
Climate widget. Sanitizer flags are in DOC-CI.md ("Host tests"); CI runs ASan and TSan.

Linux build dependencies: CMake, pkg-config, mbedTLS (required), SDL2/X11/libdrm/gbm (optional
display backends), `python3-png` and `python3-lz4` (image conversion).

ESP32 firmware, same as CI (`boards/<board>/build.sh <board> <version>` inside the board image, which
runs `idf.py -B build set-target esp32p4`, `idf.py -B build build`, then `scripts/build-flash-package.sh`):

```sh
docker build -t calaos-build-waveshare-86-panel -f boards/waveshare-86-panel/Dockerfile .
docker run --rm -v "$PWD:/work" -w /work calaos-build-waveshare-86-panel \
    bash boards/waveshare-86-panel/build.sh waveshare-86-panel 0.0.0-local
```

Inside an IDF shell: `BOARD=<board> idf.py -B build set-target esp32p4 && idf.py -B build build`.
`BOARD` is mandatory for ESP32 (CMake stops otherwise); for Linux it defaults to `linux-dev`.

- Container builds as root leave root-owned files in the worktree (`build/`, `managed_components/`,
  `pkg/`), which can block `git worktree remove`; build in a disposable directory when possible.
- A change under `hal/esp32/`, `boards/`, `cmake/`, the root `CMakeLists.txt` or `sdkconfig*` needs an
  ESP32 build to be verified; a Linux build does not compile that code.
- A new `.cpp` must be added to `cmake/sources.cmake` (shared by the Linux and ESP-IDF builds), and
  to `tests/CMakeLists.txt` if a test needs it. Forgetting it breaks the ESP32 build only.
- Host tests compile the real protocol, flux, network and websocket sources against a fake HAL
  (`tests/support/fake_hal.cpp`). A defect that cannot be fixed in the same change goes in a
  `*KnownBugs*` suite with a comment (none today).
- `APP_VERSION` comes from the environment, then `-DAPP_VERSION`, then `"git"` (`main/version.h`).
  A `"git"` build ignores OTA announcements.

## Board system

A board is `boards/<name>.cmake` plus, for ESP32, `boards/<name>/` (`sdkconfig.defaults`,
`partitions.csv`, `Dockerfile`, `build.sh`). The root `CMakeLists.txt` loads the board file before
ESP-IDF's `project.cmake`, because it feeds `SDKCONFIG_DEFAULTS`, `PARTITION_TABLE_CUSTOM_FILENAME`
and `EXCLUDE_COMPONENTS` to it. There is no Kconfig board selector.

- `BOARD_*` variables are validated (`BOARD_PLATFORM` must match the detected platform,
  `BOARD_DISPLAY_COLOR_DEPTH` in 16/24/32, `BOARD_DISPLAY_ROTATION` in 0/90/180/270) and rendered
  into `board_config.h` from `cmake/board_config.h.in`. A new variable needs both files.
- Display width and height are the useful size after rotation. The touchlcd-7/8/10 panels are
  portrait-native and run landscape with `BOARD_DISPLAY_ROTATION 90`.
- sdkconfig layers, later overriding earlier: `sdkconfig.defaults`, `sdkconfig.defaults.esp32p4`,
  `boards/<name>/sdkconfig.defaults`. `sdkconfig` and `sdkconfig.old` are git-ignored: delete them
  (or the build dir) when a layer changed and the value did not.
- Two BSPs export the same `bsp_display_*` symbols, so only one may be linked. Each board file sets
  `EXCLUDE_COMPONENTS` to drop the other: `waveshare-86-panel` uses
  `components/esp32_p4_wifi6_touch_lcd_4b` and `components/esp_lcd_st7703`; the touchlcd boards use
  `components/esp32_p4_wifi6_touch_lcd_x`, with the panel selected by `CONFIG_BSP_LCD_TYPE_*` in the
  board sdkconfig. Both BSPs are vendored on purpose: the registry versions are re-published under
  moving bounds, which used to change shipped firmware without a commit.
- `components/bsp_extra` must not declare a hard dependency on one BSP, otherwise `EXCLUDE_COMPONENTS`
  cannot work. BSP dependencies go in `main/idf_component.yml` or the board's component.
- Rotated boards need `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2` (root `sdkconfig.defaults`) for the custom PPA
  rotating flush; `waveshare-86-panel` is not rotated and overrides it to 1. The root
  `CMakeLists.txt` applies two idempotent patches to `$IDF_PATH` at configure time
  (`patches/apply_ppa_hang_fix.sh` for rotated boards, `patches/apply_dsi_clock_lane_fix.sh` for all
  ESP32 boards), so configuring modifies the shared ESP-IDF install.
- ESP-IDF is pinned to v5.5.5 in the Dockerfiles on purpose (the moving `release-v5.5` tag broke the
  86-panel display). Bump it deliberately and check every panel on hardware.
- New board checklist: `boards/<name>.cmake` (copy the closest one), `boards/<name>/` files, the
  `EXCLUDE_COMPONENTS` line, an entry in `boards/ci-boards.json`, the board name in the "Available
  boards" error message of the root `CMakeLists.txt`, and in the `workflow_dispatch` board lists of
  `build-firmware.yml` and `build-docker-images.yml` (they currently list only `all` and
  `waveshare-86-panel`, so a manual run cannot target the other boards individually). Full steps in
  DOC-CI.md.
- `luckfox-86-panel` is `enabled: false` in `ci-boards.json`; its OTA backend and relay GPIOs are TODO.

## Code map

- `flux/`: `AppDispatcher` (event queue with its own worker thread), `AppStore` (state). Events and
  their payloads are in `flux/app_event.h`. `AppStore::subscribe` callbacks do not run on the LVGL
  thread; UI updates from them go through `LvglTimer::createOneShot` as `StartupPage` does.
- `hal/`: `HAL` singleton with display, input, network, system, relay and OTA interfaces;
  `hal/esp32/` and `hal/linux/` implement them. Keep the interface in `hal/hal_*.h` free of
  platform types. The ESP32 HAL calls the BSP through `bsp_display_*` and `bsp_touch_new`.
- `network/`: UDP, HTTP and WebSocket clients over Mongoose.
- `main/`: `calaos_discovery` (UDP broadcast on port 4545), `provisioning_*` (code from
  SHA-256(MAC + salt), `POST /api/v3/provision/request`), `hmac_authenticator`,
  `calaos_websocket_manager` (endpoint `/api/v3/remote_ui/ws`, message names in
  `main/calaos_protocol.h`), `ota_manager`, widgets in `main/widgets/` registered in
  `main/widget_factory.cpp`.
- Device config (network, WiFi, server host) is a binary image with a CRC in the `config` partition
  (ESP32) or `BOARD_DEVICE_CONFIG_PATH` / `device_config.bin` (Linux): `hal/calaos_config/`.
- Provisioned credentials are stored under the key `prov.config` (NVS on ESP32, a file under
  `CALAOS_UI_CONFIG_PATH` on Linux).
- Vendored: `components/lvgl` (9.5.0), `components/mongoose`, `components/nlohmann-json`,
  `smooth_ui_toolkit/`. Do not edit them; ESP-IDF registry dependencies are in
  `main/idf_component.yml` and land in the git-ignored `managed_components/`.
- Protocol compatibility: changes to messages in `calaos_protocol.*` must match `calaos_server`
  (calaos_base, `src/bin/calaos_server/RemoteUI/`). The panel negotiates no protocol version.

## Pitfalls

- **Climate modes and range come from the server.** The mode list is `capabilities.modes` and the
  range is shown only with `target_temp_range` (`main/widgets/climate_logic.cpp`). Do not hardcode modes.
- **Fonts exist only at generated sizes.** Roboto is converted by `fonts/generate.sh` (sizes in
  `FONT_CONFIGS`, needs `lv_font_conv`) into `main/fonts/`; LVGL has no scaling fallback, so another
  size needs a new entry and a regeneration. Large sizes are ASCII-only (threshold in the script).
  `main/fonts/` also holds size-40 files that `FONT_CONFIGS` no longer lists: regenerating would not
  reproduce them. The generated fonts lack arrows, chevrons, crosses and plus/minus glyphs.
- **Icons are PNGs; some glyphs are drawn.** Icons live in `main/images/` and are converted at build
  time by `scripts/LVGLImage.py`; climate ones are rendered from `main/images/src/*.svg` by
  `scripts/render-climate-icons.sh`. Chevrons, crosses and plus/minus are drawn with `lv_line`
  (`main/widgets/climate_ui.cpp`): do not use them as text. Image variable names replace `-` by `_`.
- **UI strings are hard-coded in French**; there is no i18n layer. Keep that when adding labels.
- **TLS is not verified.** `verify_ssl = false` in the provisioning requester and the WebSocket
  manager. Do not present `wss://` as secure; see `docs/backlog/NET-01-tls-verification.md`.
- **Linux OTA is simulated** (`hal/linux/linux_ota.cpp`); only the ESP32 backend flashes.
- **Do not log secrets** (auth token, device secret, WiFi password).

## Code style

From `.github/copilot-instructions.md`: camelCase for C++ variables, functions and methods, snake_case
in C, PascalCase classes; opening brace on its own line (also for lambdas); no braces around
single-line statements, braces for multi-line ones; constructor initializer lists with the colon at
the end of the first line and one member per line; no C++ exceptions in new code, use return codes;
log errors with context; English comments, only when needed.

## Workflow

- One logical change per commit; run the host tests before proposing it, and an ESP32 build when the
  change touches ESP32 code or build files (see above).
- Pull requests target `main`. A push to `main` publishes firmware: it tags, builds and releases every
  enabled board (see DOC-CI.md). Do not push to `main` without the maintainer's approval.
- Keep `docs/` and DOC-CI.md in step when you change the CI workflows or `boards/ci-boards.json`.
