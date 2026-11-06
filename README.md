# Battlestation

Firmware and engineering notes for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B**
(ESP32-P4 rev1.3 + ESP32-C6, 7-inch 1024×600 MIPI-DSI, GT911 touch,
ES7210 microphones, ES8311 speaker path).

The `main` branch carries the stable bring-up probe: one firmware that proves
display, touch, LVGL, stereo microphone capture and hosted Wi-Fi simultaneously.
The `m5-launcher` branch adds the launcher shell, static app table and settings
placeholder. The dashboard, organism and Home Assistant integrations follow.

## Status

**Milestone:** M4 complete on `main`; M5 launcher shell implemented and
soak-validated on `m5-launcher` (manual touch/brightness check pending).

- ESP32-P4 **rev1.3**, 32 MB flash, 32 MB PSRAM, 360 MHz.
- EK79007 display + GT911 touch initialized at 180°.
- ES7210 MIC1/MIC2 stereo capture at 16 kHz, per-channel RMS/dBFS.
- Wi-Fi 6 through the C6: ESP-Hosted 1.4.7, SDIO 4-bit @ 40 MHz.
- Display pipeline pinned to `DOUBLE_DIRECT + 180°` with a local
  `esp_lvgl_adapter` 0.7.5 override.
- 386 s display/audio/touch soak and 341 s Wi-Fi + ping + touch + audio soak
  passed with zero UI stalls.
- M5 launcher shell on `m5-launcher`: four static apps, home grid, persistent
  HOME button and a live brightness slider. 100-switch self-test and 611 s soak
  passed; manual touch/brightness check pending.

Full evidence and open items: [`docs/status.md`](docs/status.md).

## Layout

| Path | Purpose |
| --- | --- |
| `main/` | Bring-up firmware: display, touch, audio, Wi-Fi, diagnostics |
| `components/esp_lvgl_adapter/` | Local 0.7.5 override — the only vendored dependency |
| `docs/` | Handbook: status, hardware, bring-up, stability, architecture, roadmap, ADRs |
| `probes/dsi/` | Standalone raw `esp_lcd` DSI stress test |
| `patches/` | Patches kept for provenance/reuse, not applied by default |
| `tools/fetch_upstream.sh` | Fetches pinned upstream sources into a gitignored cache |
| `sdkconfig.rev1_3.defaults` | Build overlay for the rev1.3 silicon on this unit |

## Quick start

```bash
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
source "$HOME/esp/esp-idf-v5.5.5/export.sh"

idf.py set-target esp32p4
idf.py menuconfig          # Wi-Fi SSID/password, mic gain, rotation

# This unit is rev1.3:
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults" \
       -D SDKCONFIG="$PWD/sdkconfig.rev1_3" build

idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Use the USB-C port labeled **“USB TO UART”** (CH343P). The Type-A port is a USB
host and cannot flash the board. See [`docs/bringup.md`](docs/bringup.md) for
the full sequence, revision handling and troubleshooting.

## Documentation

| Document | Contents |
| --- | --- |
| [`docs/status.md`](docs/status.md) | Current verified state, known warnings, next actions |
| [`docs/progress.md`](docs/progress.md) | Dated engineering log |
| [`docs/hardware.md`](docs/hardware.md) | Verified board architecture, pin map, USB, revision traps |
| [`docs/bringup.md`](docs/bringup.md) | macOS/ESP-IDF setup, build, flash, acceptance checks |
| [`docs/stability.md`](docs/stability.md) | Display-freeze root cause, isolation matrix, chosen pipeline |
| [`docs/architecture.md`](docs/architecture.md) | Target application architecture and portability boundary |
| [`docs/roadmap.md`](docs/roadmap.md) | M0–M10 with status and acceptance criteria |
| [`docs/decisions/`](docs/decisions/) | Architecture decision records (ADRs) |
| [`docs/external/upstream.md`](docs/external/upstream.md) | Dependency provenance and update policy |
| [`docs/research/`](docs/research/) | Frozen 2026-11-06 research handoff (source material) |

## Dependency policy

The repository does **not** vendor upstream projects. Dependencies are resolved
through the ESP-IDF Component Manager and pinned in `dependencies.lock`;
`tools/fetch_upstream.sh` can materialize reference sources into a gitignored
cache for inspection.

The single exception is `components/esp_lvgl_adapter/`, kept locally because
the Waveshare BSP pins an older adapter line with a display deadlock. The
reason, scope and removal condition are recorded in
[ADR-0003](docs/decisions/0003-local-adapter-override.md) and
[`docs/external/upstream.md`](docs/external/upstream.md).

Hardware-specific knowledge is deliberately kept at the BSP/service boundary so
the application, audio analysis and organism engine can be ported to another
board later. See [ADR-0007](docs/decisions/0007-portability-boundary.md).
