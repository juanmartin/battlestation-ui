# ADR-0016 — Organism uses the adapter's direct framebuffer path

**Status:** Accepted  
**Date:** 2026-11-09

## Context

The first M8 integration rendered the organism into a triple-buffered RGB565
image and let LVGL display it. On hardware this reached only ~6 fps: LVGL
decoded/drew a full-screen 1.2 MB image and the display pipeline rotated/copied
it every frame. Engine step time was also inflated to ~43–54 ms by LVGL
preemption on the same core.

The adapter already provides a **dummy draw** mode for applications that own the
whole display: `esp_lv_adapter_set_dummy_draw()`,
`esp_lv_adapter_dummy_draw_get_free_buf()` and
`esp_lv_adapter_dummy_draw_flush_buf()`. It bypasses LVGL rendering and hands
the application a panel-facing buffer.

## Decision

The organism app uses dummy draw, not an LVGL image:

- On mount, enable dummy draw; on unmount, disable it.
- The engine renders **directly into the adapter's free frame buffer** via
  `organism_engine_step_into(engine, dst, s_last)`. There is no extra 1.2 MB
  copy and no engine-owned triple buffer.
- `s_last` is the previously flushed buffer; it is the trail source. If the
  adapter returns the same buffer, the frame is skipped rather than torn.
- The engine renders in the panel's **physical orientation** (`rotate_180`),
  so the app performs a plain display-buffer write. The 180° transform is in
  the particle plot, not the copy loop.
- A small HOME hint is drawn into the top-right of the buffer and a matching
  touch hotspot returns to the launcher. The LVGL HOME button is not rendered
  in dummy-draw mode.
- PPA acceleration remains disabled; the engine and copy are CPU/PSRAM only.

## Consequences

- Target result: **30.2 fps** with 3072 particles; step ~20.7 ms and flush
  ~11.4 ms. The LVGL image path was ~6 fps.
- Dummy draw is global display state. Mount/unmount must enable/disable it and
  handle failures; the 100-switch self-test exercises this 25 times.
- The organism task must not call LVGL. It only reads audio/touch snapshots and
  the adapter's dummy-draw API.
- Only 0°/180° panel rotation is handled by the pre-rotation path.
- The visible HOME affordance is drawn by the engine, not LVGL.

See [`../architecture.md`](../architecture.md#organism-rendering) and
[`../m8-organism-validation.md`](../m8-organism-validation.md).
