# ADR-0010 — Software stack: ESP-IDF + Waveshare BSP + LVGL 9.5

**Status:** Accepted  
**Date:** 2026-11-06

## Context

The board's display, touch, audio routing and C6 transport are encoded in the
Waveshare BSP. The BSP is pinned to specific ESP-IDF, LVGL and adapter versions.
Generic ESP32-P4 examples and community ports target different panels or older
LVGL lines.

## Decision

Use:

| Layer | Choice |
| --- | --- |
| Framework | **ESP-IDF v5.5.5** |
| Board support | **Waveshare BSP 3.0.1** |
| UI | **LVGL 9.5.0** (exact) |
| Display adapter | **`esp_lvgl_adapter` 0.7.5** via local override |
| Audio | **`esp_codec_dev`** via the BSP |
| Wi-Fi transport | **`esp_hosted 1.4.7` + `esp_wifi_remote 1.2.5`** |

Do not start from a generic P4 example, Arduino, PlatformIO or a random LVGL
port.

## Rejected alternatives

| Alternative | Why not |
| --- | --- |
| Arduino | Waveshare recommends ESP-IDF for P4; weaker PSRAM/DSI/I2S control |
| PlatformIO | P4 support lags the IDF version the BSP requires |
| ESP-Brookesia as framework | Overkill for three tightly coupled apps; factory line is old |
| LVGL 8.4 / 9.2 | BSP 3.0.1 expects 9.5 APIs; mixing breaks the adapter |
| Legacy `esp_lvgl_port` | Duplicate display/input management beside the adapter |
| Direct register programming | No benefit; the BSP exposes the needed handles |

## Consequences

- The BSP remains the hardware boundary; do not fork it.
- Version upgrades must be coordinated across IDF, LVGL, adapter and BSP.
- The local adapter override is required until the BSP dependency is widened
  ([ADR-0003](0003-local-adapter-override.md)).
- Arduino examples remain useful only for pin cross-checks.

See [`../external/upstream.md`](../external/upstream.md).
