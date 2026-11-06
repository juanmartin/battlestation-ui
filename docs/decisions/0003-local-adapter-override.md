# ADR-0003 — Keep a local 0.7.5 adapter override

**Status:** Accepted, temporary  
**Date:** 2026-11-06  
**Remove when:** Waveshare widens the BSP dependency from `~0.6` and the
rotated display path is verified on this unit

## Context

Waveshare BSP 3.0.1 resolves `espressif/esp_lvgl_adapter 0.6.4`, which has a
known DSI framebuffer-ownership deadlock (Espressif issue #793), fixed in 0.7.3.
The BSP manifest still says `~0.6`, so a clean build would pull the broken line.

Adapter 0.7.5 fixes the #793 race but still deadlocks on this unit's rotated
partial-refresh path. `DOUBLE_DIRECT + 180°` is stable with 0.7.5.

## Decision

Vendor **one** component: `components/esp_lvgl_adapter/` at 0.7.5, and point at
it from `main/idf_component.yml`:

```yaml
espressif/esp_lvgl_adapter:
  version: "==0.7.5"
  override_path: "../components/esp_lvgl_adapter"
```

This is the only vendored dependency in the repository. It is an explicit,
documented exception to the no-vendoring policy in
[ADR-0004](0004-external-dependencies.md).

## Consequences

- Builds are reproducible even if the BSP manifest still resolves 0.6.x.
- The repository carries ~2.7 MB of upstream source; upgrades require replacing
  the directory and re-verifying the display soak.
- The override must be removed or refreshed when the BSP widens its dependency.
- Do not edit upstream files casually; record local changes separately.

Provenance: `espressif/esp-iot-solution` commit `5fbb259`, component path
`components/display/tools/esp_lvgl_adapter`, version 0.7.5. The bundled PPA patch
is upstream work by Espressif, not local code; it patches ESP-IDF, not the
adapter, and is not applied by default.
