# Calaos Remote UI

Firmware for the Calaos wall panels. It runs a touch UI built with
[LVGL](https://lvgl.io) on ESP32-P4 boards (ESP-IDF), and the same code also builds as a
desktop Linux application used as a simulator and for host tests.

A panel finds a [calaos_server](https://github.com/calaos/calaos_base) on the local network,
is paired with it using a short code, then keeps a WebSocket open to show and control the I/Os
that the server assigns to it (lights, shutters, temperature, scenarios, climate, clock).

## Supported boards

Boards are selected with `-DBOARD=<name>` (or the `BOARD` environment variable); each one is
described by `boards/<name>.cmake`.

| Board | Platform | Display | Network | Relays | Built by CI |
|---|---|---|---|---|---|
| `waveshare-86-panel` | ESP32-P4 | 720x720 (ST7703) | Ethernet, WiFi | 2 | yes |
| `waveshare-touchlcd-7` | ESP32-P4 | 1280x720 landscape (ILI9881C panel, rotated) | WiFi | none | yes |
| `waveshare-touchlcd-8` | ESP32-P4 | 1280x800 landscape (JD9365 panel, rotated) | WiFi | none | yes |
| `waveshare-touchlcd-10` | ESP32-P4 | 1280x800 landscape (JD9365 panel, rotated) | WiFi | none | yes |
| `linux-dev` | Linux (SDL, X11, DRM, fbdev, ...) | 720x720 | host network | none | host tests only |
| `luckfox-86-panel` | Linux (Buildroot submodule) | 720x720 | Ethernet, WiFi | 2 | no (disabled in `boards/ci-boards.json`) |

On the ESP32-P4 boards WiFi goes through an ESP32-C6 coprocessor (ESP-Hosted over SDIO).
The Luckfox port is incomplete: its OTA backend is a stub and its relay GPIOs are placeholders.

## Features

- Server discovery by UDP broadcast (port 4545), or a fixed server address from the device
  configuration.
- Pairing with a short code shown on the panel, then HMAC-SHA256 authenticated WebSocket
  (`/api/v3/remote_ui/ws`).
- Pages and widget grid pushed by the server: light switch, temperature, scenario, shutter,
  climate (thermostat) and clock widgets, plus a screensaver.
- Brightness, page change and notification commands from the server; state of the board relays
  reported back.
- Over-the-air firmware update announced by the server, with SHA-256 check and dual-slot
  rollback on ESP32.
- On-device network settings page (Ethernet or WiFi, DHCP or static IP, server address).

UI strings are currently hard-coded in French.

Not done yet: TLS certificate verification (connections to the server and OTA downloads do not
verify certificates), and OTA on Linux boards (the Linux backend only simulates it). See
`docs/backlog/NET-01-tls-verification.md`.

## Building

### ESP32-P4 firmware

Builds are done with ESP-IDF **v5.5.5**, pinned in the board Dockerfiles
(`boards/*/Dockerfile`, image `espressif/idf:v5.5.5`). Use that image rather than a host
install: the build patches the shared `$IDF_PATH` (see `patches/README.md`), and a different
ESP-IDF release has already broken the 86-panel display once.

The way CI builds a board (`waveshare-86-panel` shown):

```sh
docker build -t calaos-build-waveshare-86-panel -f boards/waveshare-86-panel/Dockerfile .
docker run --rm -v "$PWD:/work" -w /work calaos-build-waveshare-86-panel \
    bash boards/waveshare-86-panel/build.sh waveshare-86-panel 0.0.0-local
```

CI pulls the prebuilt image `ghcr.io/calaos/calaos-build-<board>:latest` instead of building it.
The script runs `idf.py -B build set-target esp32p4 && idf.py -B build build` with `BOARD`
and `APP_VERSION` exported, then `scripts/build-flash-package.sh`. Outputs are
`build/calaos-remote-ui.bin` and `pkg/*-flash.zip`. Inside an ESP-IDF shell the equivalent is:

```sh
export BOARD=waveshare-86-panel
idf.py -B build set-target esp32p4
idf.py -B build build
```

Without `APP_VERSION` the firmware reports version `git`, and development builds with that
version ignore OTA announcements.

`.devcontainer/` provides a VS Code dev container on the same ESP-IDF image with SDL2, X11 and
QEMU added.

### Linux simulator

Dependencies: CMake, a C/C++ toolchain, pkg-config, mbedTLS, Python 3 with `pypng` and `lz4`
(image conversion), and at least one display backend (SDL2, X11, libdrm/gbm, or framebuffer).
Display and input backends are detected at configure time.

```sh
./build_linux.sh    # board linux-dev, output build_linux/calaos-remote-ui
```

```sh
build_linux/calaos-remote-ui --list-backends
build_linux/calaos-remote-ui --display-backend sdl --server-ip 192.168.1.100
```

Also available through environment variables: `CALAOS_DISPLAY_BACKEND`, `CALAOS_INPUT_BACKEND`,
`CALAOS_SERVER_IP` (skips discovery) and `CALAOS_UI_CONFIG_PATH` (state directory, default
`~/.config/calaos_remote_ui`).

### Host tests

```sh
cmake -S . -B build_linux -DBUILD_LINUX=ON -DBOARD=linux-dev -DBUILD_HOST_TESTS=ON
cmake --build build_linux -j"$(nproc)"
ctest --test-dir build_linux --output-on-failure
```

The tests use a fake HAL and an in-memory LVGL display; GoogleTest is fetched by CMake.
Details and sanitizer builds are in [DOC-CI.md](DOC-CI.md).

## Flashing and pairing

### First flash

Each CI release carries a flash package `calaos-remote-ui-<board>-<version>-flash.zip`
(bootloader, partition table, OTA data, application, empty config partition and a
`manifest.json` with the esptool parameters). Calaos Installer can flash it, or run esptool by
hand using the offsets in the manifest. The command is in
[DOC-CI.md](DOC-CI.md#manual-flashing-with-esptool).

During development you can also flash from the build tree with `idf.py -B build flash` (set
`ESPPORT`).

### Network and server settings

The board has a `config` partition holding a small binary image (magic `CALO`, version, CRC32,
JSON payload) with: network interface (`ethernet` or `wifi`), DHCP or static IP, WiFi
credentials, and optionally a server host, port and SSL flag. Without a valid image the board
uses Ethernet with DHCP and discovers the server by broadcast. The settings page on the panel
can also write it (`saveDeviceConfig`). On Linux boards the same image is read from `BOARD_DEVICE_CONFIG_PATH`, or
from `device_config.bin` in the state directory for `linux-dev`.

### Pairing with calaos_server

1. The panel connects to the network, syncs time over NTP, then looks for the server (UDP
   broadcast, 2 s interval, 30 s timeout) unless a server host is configured.
2. An unpaired panel shows a 6-character provisioning code derived from its MAC address and a
   random salt.
3. On the server side, create a `RemoteUI` I/O (Calaos Installer) with that code as its
   `provisioning_code`.
4. The panel keeps polling `POST /api/v3/provision/request` until the server recognises the
   code. The answer carries a device id, an auth token and a device secret, which the panel
   stores.
5. The panel opens the WebSocket. Each handshake carries `Authorization: Bearer <token>`,
   `X-Auth-Timestamp`, `X-Auth-Nonce` and `X-Auth-HMAC`, where the HMAC is
   `HMAC-SHA256(device_secret, token:timestamp:nonce)`. If the server rejects the token or the
   HMAC, the panel drops its credentials, shows a new code and restarts discovery.

The server side is described in
[calaos_base docs/07_remoteui.md](https://github.com/calaos/calaos_base/blob/master/docs/07_remoteui.md)
(in French).

## OTA updates

Releases are published as `.deb` packages named `calaos-remote-ui-<board>`, installing
`calaos-remote-ui.bin` and a `manifest.json` (hardware id, version, SHA-256) under
`/usr/share/calaos/remote-ui/firmwares/<board>/`. The server scans its firmware directory and
announces a matching firmware (`remote_ui_fw_update_available`) to connected panels. The panel
ignores announcements for another `hardware_id` or for the version it already runs, then
downloads the image from the server with the same HMAC headers, checks the SHA-256, writes the
inactive `ota_0`/`ota_1` slot and reboots. The new image must mark itself valid after boot,
otherwise the bootloader rolls back (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).

## Repository layout

| Path | Content |
|---|---|
| `main/` | Application: startup/pairing page, pages and widgets, WebSocket manager, provisioning, OTA, generated Roboto fonts and PNG icons |
| `flux/` | Flux-style state store and event dispatcher shared by the UI and the network code |
| `hal/` | Hardware abstraction (display, input, network, system, relays, OTA) with `esp32/` and `linux/` implementations, and the device config parser in `calaos_config/` |
| `network/` | UDP, HTTP and WebSocket clients (Mongoose based) |
| `boards/` | One `<board>.cmake` per board, plus per-board sdkconfig, partition table, Dockerfile and build script; `ci-boards.json` lists the boards CI builds |
| `components/` | Vendored LVGL, Mongoose, nlohmann-json, Waveshare BSPs and panel driver, `bsp_extra` |
| `smooth_ui_toolkit/` | Animation and LVGL C++ helper library |
| `cmake/` | Shared source list and `board_config.h` template |
| `patches/` | Idempotent fixes applied to ESP-IDF during configure |
| `fonts/`, `scripts/` | Font generation, image conversion, packaging and version scripts |
| `tests/` | Host unit and widget tests |
| `docs/` | Technical-debt audit and refactoring backlog |

## CI and releases

- Pull requests to `main`: Linux simulator build and host tests, ASan and TSan runs, and an
  ESP32 build of `waveshare-86-panel` (`.github/workflows/pull-request.yml`).
- Every push to `main` builds all enabled boards, creates the tag `<board>-X.Y.Z-dev.N` and a
  GitHub prerelease with the `.deb` and the flash package
  (`.github/workflows/build-firmware.yml`). Stable versions are cut by running that workflow
  manually with a major, minor or patch increment.
- Board build images are rebuilt on pushes that touch `boards/**/Dockerfile`.

Versioning, package layout and flash package format are documented in [DOC-CI.md](DOC-CI.md).

## More documentation

- [DOC-CI.md](DOC-CI.md): CI workflows, versioning, packages, adding a board to CI.
- [patches/README.md](patches/README.md): why ESP-IDF is patched and when.
- [docs/AUDIT.md](docs/AUDIT.md), [docs/BOARD.md](docs/BOARD.md): audit and refactoring backlog
  (planned work; ticket status may lag the code).
- [AGENTS.md](AGENTS.md): guide for contributors and coding agents.

## License

This repository has no `LICENSE` file yet. Calaos itself is distributed under GPL-3.0 or later;
vendored components under `components/` and `smooth_ui_toolkit/` keep their own licenses.
