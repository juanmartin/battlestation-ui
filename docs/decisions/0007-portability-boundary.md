# ADR-0007 — Hardware boundary for future board ports

**Status:** Accepted  
**Date:** 2026-11-06  
**Scope:** design rule; enforced as services are implemented in M5–M6

## Context

The current board may be replaced by another ESP32-P4 panel board, or by a
different SoC/display combination later. The valuable first-party work is the
application model, audio analysis, UI and organism engine — not the board
wiring. Without an explicit boundary, BSP pin names, panel timing and codec
handles leak into app code and make a port a rewrite.

## Decision

Define four layers:

```text
apps/ + engine/          portable: LVGL UI, app manager, organism, HA client
        │
services/                stable APIs; one owner per hardware resource
        │
board backend            board-specific: BSP, pins, panel, touch, codec, C6
        │
ESP-IDF / BSP / drivers
```

Rules:

1. `apps/` and `engine/` must not include `bsp_*`, `esp_lcd_*`, codec or
   ESP-Hosted headers.
2. `engine/` must not include LVGL. It renders to an RGB565 buffer and builds on
   macOS with a stub canvas.
3. Services expose semantic APIs (`display_set_brightness`,
   `audio_get_features`, `net_get_state`), not handles or pins.
4. All board-specific constants live in one board config surface (Kconfig or
   `board_config.h`), not scattered across modules.
5. Audio analysis, HA JSON and the organism engine stay pure C where practical,
   so they can be unit-tested off-target.
6. A port is allowed to replace the board backend and service implementations.
   If it must touch `apps/`, `engine/` or analysis, the boundary has leaked.

## Consequences

- The current probe can remain monolithic; the boundary is introduced with the
  service split at M5–M6.
- A future board port requires: new BSP, new `display_service` backend, new
  `audio_service` backend, new `net_service` backend.
- Some abstraction overhead in service APIs; keep them narrow and concrete.
- Portability is testable: the organism engine and analysis must build and run
  on the host.

See [`../architecture.md`](../architecture.md#portability-boundary).
