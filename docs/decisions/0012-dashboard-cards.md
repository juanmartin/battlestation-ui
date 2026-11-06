# ADR-0012 — Dashboard cards and fullscreen detail views

**Status:** Accepted  
**Date:** 2026-11-06

## Context

The dashboard needs several independent data surfaces: clock, weather, Wi-Fi and
audio status, and four Home Assistant light controls. Each is useful as a compact
card but also needs a larger view with more detail. Making every card a separate
app would multiply lifecycle and navigation code.

## Decision

The dashboard is **one app that owns a card grid**:

- A static card registry: `clock`, `weather`, `status`, `light_0..light_3`.
- Each card mounts a compact LVGL widget into the grid.
- Tapping a card opens an internal **detail view** with a larger graph or more
  controls; back returns to the grid.
- The dashboard data model stays alive underneath; the detail view is a
  presentation state, not a separate app.
- Cards read from services (`net_service`, `ha_client`, `audio_analysis`), never
  from hardware directly.
- The first four light cards are fixed; entity IDs are configurable. Until M10
  they come from Kconfig, then from NVS.
- Weather prefers an HA weather entity; an external provider can be added later
  behind the same card API.

## Consequences

- One lifecycle for the dashboard; detail views are cheap to open and close.
- Card widgets must be reusable and layout-aware at 1024×600.
- The dashboard needs a small internal view stack (grid ↔ detail).
- Adding a card is a registry entry plus a widget, not a new app.
- M10 settings must notify the dashboard when entity IDs or HA credentials change.

See [`../architecture.md`](../architecture.md#dashboard-composition) and
[`../roadmap.md`](../roadmap.md).
