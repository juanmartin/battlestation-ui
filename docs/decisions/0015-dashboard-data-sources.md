# ADR-0015 — Dashboard data sources and Home Assistant configuration

**Status:** Accepted  
**Date:** 2026-11-09

## Context

M7 adds a dashboard with a clock, weather, Wi-Fi/audio status and four Home
Assistant light controls. The device must remain useful when HA is not
configured, and runtime provisioning is not scheduled until M10.

## Decision

- **Clock:** SNTP (`pool.ntp.org`) with a compile-time timezone
  (`CONFIG_APP_TIMEZONE`). The UI shows “not synced” until the system time is
  valid; it does not fake a time.
- **Weather:** prefer a Home Assistant weather entity. No external API key in
  M7. When HA is unavailable, show a placeholder and keep the card layout.
- **Lights:** four fixed cards; each has on/off and brightness (color
  temperature later if the entity supports it). Entity IDs come from Kconfig
  until M10 adds runtime settings.
- **Transport:** HA WebSocket for `state_changed` + `call_service`, REST for the
  initial state and fallback. One client, one connection, no UI blocking.
- **Offline behavior:** the dashboard always renders. HA cards show “not
  configured/offline” and their controls report unavailable; no crash, no fake
  state.
- **Config placeholders:** `CONFIG_APP_HA_URL`, `CONFIG_APP_HA_TOKEN`,
  `CONFIG_APP_LIGHT_ENTITY_0..3`, `CONFIG_APP_WEATHER_ENTITY`,
  `CONFIG_APP_TIMEZONE`.

## Consequences

- M7a can land the card grid, detail views, clock/status and four light controls
  with the HA client stubbed; M7b wires the client and weather.
- Tokens stay out of git; the Kconfig default is empty and the local credentials
  live in `sdkconfig.wifi.local`-style local overlays or NVS later.
- M10 replaces Kconfig entity IDs with runtime settings and must notify the
  dashboard when they change.
- No external weather dependency in M7; adding one later is a separate decision.

See [`../architecture.md`](../architecture.md#dashboard-composition) and
[`../roadmap.md`](../roadmap.md).
