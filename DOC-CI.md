# CI/CD Documentation

## Overview

The CI builds firmware for each supported board, packages it into a `.deb`, and creates a GitHub Release with independent per-board versioning.

### Workflow: `build-firmware.yml`

**Triggers:**

- **Push to `main`** — Builds all boards, creates prerelease versions (`X.Y.Z-dev.N`)
- **Manual dispatch** — Select a specific board (or all) and a version increment (major / minor / patch)

**Jobs:**

| Job | Purpose |
|---|---|
| `matrix-setup` | Reads `boards/ci-boards.json` and builds the build matrix |
| `build` | Pulls the pre-built Docker image and builds the firmware |
| `package-and-release` | Packages the `.bin` into a `.deb`, creates a git tag and GitHub Release |

### Workflow: `build-docker-images.yml`

**Triggers:**

- **Push to `main`** with changes in `boards/**/Dockerfile`
- **Manual dispatch** — Select a specific board image to rebuild (or all)

Builds and pushes Docker images to `ghcr.io`. These images are then pulled by `build-firmware.yml` instead of being built from scratch each time, saving ~10-15 minutes per firmware build.

## Board Configuration

All CI-enabled boards are declared in `boards/ci-boards.json`:

```json
[
  {
    "name": "waveshare-86-panel",
    "platform": "ESP32",
    "dockerfile": "boards/waveshare-86-panel/Dockerfile",
    "docker_image": "ghcr.io/calaos/calaos-build-waveshare-86-panel:latest",
    "build_script": "boards/waveshare-86-panel/build.sh",
    "bin_path": "build/calaos-remote-ui.bin",
    "description": "Waveshare ESP32-P4 86-Panel firmware"
  }
]
```

| Field | Description |
|---|---|
| `name` | Board identifier, used for tags, package names, and install paths |
| `platform` | Informational (ESP32, LINUX, etc.) |
| `dockerfile` | Path to the Docker build environment for this board |
| `docker_image` | Pre-built image on ghcr.io (pulled by CI, built by `build-docker-images.yml`) |
| `build_script` | Path to the board-specific build script (called inside the Docker container) |
| `bin_path` | Path to the firmware binary produced by the build |
| `flash_package` | (ESP32 only) If `true`, build a flash package `.zip` for initial flashing via esptool |
| `partition_table` | (ESP32 only) Path to the partition table CSV, used by the flash package script |
| `description` | Human-readable description used in release notes |

## Adding a New Board

1. **Create the board directory and files:**

   ```
   boards/<board-name>/
   ├── Dockerfile      # Build environment (toolchain, dependencies)
   └── build.sh        # Build script called by CI
   ```

2. **Write the build script** (`boards/<board-name>/build.sh`):

   The script receives two arguments: `<board-name>` and `<version>`.
   It must produce the firmware binary at the path declared in `bin_path`.

   Example for an ESP-IDF board:
   ```bash
   #!/usr/bin/env bash
   set -euo pipefail
   BOARD="$1"; VERSION="$2"
   source /opt/esp/idf/export.sh
   idf.py -B build set-target esp32p4
   idf.py -B build build -DBOARD="${BOARD}" -DAPP_VERSION="${VERSION}"
   ```

   Example for a Buildroot board:
   ```bash
   #!/usr/bin/env bash
   set -euo pipefail
   BOARD="$1"; VERSION="$2"
   make BR2_EXTERNAL=/work/boards/${BOARD}/external ${BOARD}_defconfig
   make -j$(nproc)
   cp output/images/firmware.bin build/calaos-remote-ui.bin
   ```

3. **Add an entry in `boards/ci-boards.json`** with the appropriate fields.
   Set `docker_image` to `ghcr.io/calaos/calaos-build-<board-name>:latest`.

4. **Add the board to the workflow dispatch options** in both:
   - `.github/workflows/build-firmware.yml` (board selection for releases)
   - `.github/workflows/build-docker-images.yml` (board selection for image rebuilds)

   ```yaml
   workflow_dispatch:
     inputs:
       board:
         options:
           - all
           - waveshare-86-panel
           - <new-board-name>      # ← add here
   ```

5. **Push the Dockerfile** — The `build-docker-images.yml` workflow will automatically build and push the image to ghcr.io on the first push that includes the new Dockerfile.

## Docker Images

Build environment images are hosted on GitHub Container Registry (`ghcr.io`) to avoid rebuilding them on every firmware build (~10-15 min saved).

### How it works

1. `build-docker-images.yml` triggers when a `boards/**/Dockerfile` changes on `main` (or via manual dispatch)
2. It builds the image using Docker Buildx with GitHub Actions cache
3. The image is pushed to `ghcr.io/calaos/calaos-build-<board-name>` with tags `latest` and the commit SHA
4. `build-firmware.yml` pulls the pre-built image via `docker_image` from `ci-boards.json`
5. If the pull fails (e.g. first run before any image exists), it falls back to building from the Dockerfile

### Manual rebuild

To force-rebuild a Docker image without changing the Dockerfile, use the manual dispatch on `build-docker-images.yml` and select the board.

## Versioning

Each board has its own independent version tracked via git tags.

### Tag Format

```
<board-name>-<version>
```

Examples:
- `waveshare-86-panel-1.0.0`
- `waveshare-86-panel-1.0.1-dev.3`
- `luckfox-86-panel-0.2.0`

### Version Fragments

| Mode | Fragment | Example: `1.0.0` → | Example: `1.0.1-dev.3` → |
|---|---|---|---|
| Push to main | `prerelease` | `1.0.1-dev.1` | `1.0.1-dev.4` |
| Manual dispatch | `patch` | `1.0.1` | `1.0.1` (promotes) |
| Manual dispatch | `minor` | `1.1.0` | `1.1.0` |
| Manual dispatch | `major` | `2.0.0` | `2.0.0` |

The version bump logic is in `scripts/bump-version.sh`. It filters tags by board prefix so boards are versioned independently.

### First Release

When no tags exist for a board, the first version will be:
- `0.0.1-dev.1` (prerelease on push)
- `0.0.1` (manual patch/minor/major)

## Debian Package

### Package Name

```
calaos-remote-ui-<board-name>
```

Example: `calaos-remote-ui-waveshare-86-panel`

### Package Contents

```
/usr/share/calaos/remote-ui/firmwares/<board-name>/
├── calaos-remote-ui.bin    # Firmware binary
└── manifest.json           # Metadata (version, checksum, hardware_id, etc.)
```

### Manifest Format

```json
{
    "schema_version": 1,
    "hardware_id": "waveshare-86-panel",
    "version": "1.0.5",
    "firmware_file": "calaos-remote-ui.bin",
    "checksum_sha256": "abcdef...",
    "name": "Calaos Remote UI",
    "release_date": "2026-02-10T14:54:39Z",
    "description": "waveshare-86-panel firmware"
}
```

### Architecture

All packages use `Architecture: all` since they contain firmware blobs, not native executables.

### Version in .deb

Prerelease versions use `~` instead of `-` for correct dpkg sorting:
- `1.0.1-dev.3` → deb version `1.0.1~dev.3` (sorts before `1.0.1`)

## Scripts

| Script | Purpose |
|---|---|
| `scripts/bump-version.sh <board> <fragment>` | Compute next version from git tags for a board |
| `scripts/build-deb.sh <board> <version> <bin-path>` | Build a `.deb` from a firmware binary |
| `scripts/build-flash-package.sh <board> <version> <build-dir> <partition-csv>` | Build a flash package `.zip` for initial flashing via esptool (ESP32 only) |
| `boards/<board>/build.sh <board> <version>` | Board-specific firmware build (runs in Docker) |

## GitHub Releases

Each release is tagged `<board-name>-<version>` and contains:
- The `.deb` package as a release asset
- For ESP32 boards: the flash package `.zip` as an additional release asset
- Release notes with board name, version, and platform

Prerelease versions (`-dev.N`) are marked as GitHub prereleases.

Stable releases trigger the webhook to update the repository cache.

## Flash Package (ESP32 only)

For ESP32 boards, CI produces a flash package alongside the `.deb`. This package contains all binaries needed for initial device flashing via esptool.

### Package Name

```
calaos-remote-ui-<board-name>-<version>-flash.zip
```

Example: `calaos-remote-ui-waveshare-86-panel-1.0.5-flash.zip`

### Package Contents

```
bootloader.bin          # Second-stage bootloader
partition-table.bin     # Partition table
ota_data_initial.bin    # OTA data (selects ota_0 slot)
calaos-remote-ui.bin    # Application firmware
config.bin              # Config partition placeholder (64 KiB of 0xFF)
manifest.json           # Metadata and esptool parameters
```

### Manifest Format

```json
{
    "schema_version": 1,
    "board": "waveshare-86-panel",
    "version": "1.0.5",
    "release_date": "2026-03-24T12:00:00Z",
    "esptool": {
        "chip": "esp32p4",
        "flash_mode": "dio",
        "flash_size": "32MB",
        "flash_freq": "40m",
        "baudrate": 921600,
        "before": "default_reset",
        "after": "hard_reset"
    },
    "binaries": [
        {
            "filename": "bootloader.bin",
            "offset": "0x2000",
            "checksum_sha256": "abcdef...",
            "checksum_md5": "123456..."
        },
        {
            "filename": "partition-table.bin",
            "offset": "0xc000",
            "checksum_sha256": "...",
            "checksum_md5": "..."
        },
        {
            "filename": "ota_data_initial.bin",
            "offset": "0x13000",
            "checksum_sha256": "...",
            "checksum_md5": "..."
        },
        {
            "filename": "calaos-remote-ui.bin",
            "offset": "0x20000",
            "checksum_sha256": "...",
            "checksum_md5": "..."
        },
        {
            "filename": "config.bin",
            "offset": "0x1E20000",
            "checksum_sha256": "...",
            "checksum_md5": "..."
        }
    ]
}
```

### Manual Flashing with esptool

The manifest contains all information needed to construct the esptool command:

```bash
esptool.py --chip esp32p4 --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 32MB --flash_freq 40m \
    0x2000    bootloader.bin \
    0xc000    partition-table.bin \
    0x13000   ota_data_initial.bin \
    0x20000   calaos-remote-ui.bin \
    0x1E20000 config.bin
```

### Calaos Installer Integration

Calaos Installer reads the `manifest.json` from the flash package to automatically determine all esptool parameters, binary offsets, and checksums. The user selects the `.zip` file and Calaos Installer handles the rest.

### Config Partition Placeholder

The `config.bin` file is a placeholder filled with `0xFF` bytes (erased flash state). It ensures the config partition is in a clean state after initial flashing. Calaos Installer can replace this placeholder with a real provisioning configuration when the user provides network/server settings (see `PROV_FEATURE.md`).
