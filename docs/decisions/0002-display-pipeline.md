# ADR-0002 — Display pipeline: `DOUBLE_DIRECT` at 180°, PPA off

**Status:** Accepted  
**Date:** 2026-11-06  
**Revisit when:** the adapter's rotated partial-refresh path is fixed upstream

## Context

The BSP default is `TRIPLE_PARTIAL` with three DPI framebuffers. On this unit,
the adapter's rotated partial-refresh path deadlocks after ~20–26 s: the LVGL
worker blocks in an unbounded framebuffer wait while audio, heap, PSRAM and
temperature remain healthy. Raw DSI is stable, so the panel/PSRAM/power path is
not at fault.

Measured isolation:

| Mode | Result |
| --- | --- |
| `TRIPLE_PARTIAL`, 0° | 75 s stable |
| `TRIPLE_PARTIAL`, 180° | deadlock ~21–26 s (0.6.4 and 0.7.5) |
| `TRIPLE_FULL`, 180° | 75 s stable with touch |
| `DOUBLE_DIRECT`, 180° | 386 s stable, ~20 fps idle / 60 fps touch |
| `DOUBLE_DIRECT` + Wi-Fi/ping/touch/audio | 341 s stable, zero stalls |

## Decision

Default to **`ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT` at 180°** with
`esp_lvgl_adapter` 0.7.5. Keep `TRIPLE_FULL` as the slower fallback. Treat
`TRIPLE_PARTIAL` as unsafe on this unit even though it is the BSP default.

Keep **PPA acceleration disabled**. If PPA is revisited, apply the adapter's
PPA bypass patch to ESP-IDF, set `LV_DRAW_SW_DRAW_UNIT_CNT == 1`, and benchmark
before changing the default.

## Consequences

- Stable display with two framebuffers instead of three; less PSRAM used.
- Idle FPS is ~20 because the probe invalidates labels at 20 Hz; touch raises it
  to ~60 fps. Do not read idle FPS as a ceiling.
- The chosen mode is a local workaround, not an upstream guarantee.
- Re-evaluate when the BSP adapter dependency is widened and rotated partial
  refresh is verified on this unit.

See [`../stability.md`](../stability.md).
