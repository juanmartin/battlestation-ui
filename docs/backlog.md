# Post-plan backlog

**Captured:** 2026-11-09  
**Status:** unscheduled; these land after the current M9–M11 plan.

These are product notes, not commitments. Several have open questions at the
bottom.

## Localization

- English / Castellano UI, selectable at runtime.
- Persist the language in NVS; apply without a reboot where practical.
- Cover app titles, body text, settings, HA state labels, date/time formats and
  diagnostics.
- Watch LVGL text width: Spanish strings are longer; check card/tile clipping.

## Clock and time

- Clock with arrows on the right (adjust? date navigation? timezone picker?
  clarify the intended interaction).
- Default to UTC-3 (America/Argentina/Buenos_Aires) and/or expose a timezone
  picker.
- If a picker is added, handle DST correctly rather than a fixed offset.
- Current M7 behavior: SNTP plus a POSIX `TZ` string from Kconfig; no runtime
  timezone UI.

## Weather source

- Current M7 weather comes from a Home Assistant weather entity over REST/WS.
- Evaluate a direct provider (for example Open-Meteo or another service) with a
  configurable location, so weather works when HA is absent.
- Decide priority/fallback: HA entity first, direct provider fallback (or the
  reverse), plus caching and offline state.

## Navigation and “home”

- Make the **dashboard** the default/home screen.
- Add a **MENU** button that opens the app grid / current launcher home.
- Clarify final semantics:
  - Is MENU global (available from every app) or only on the dashboard?
  - Does HOME still exist, and does it mean dashboard while MENU means the app
    grid?
- Current M5/M7 behavior: launcher is home; dashboard is an app; HOME returns to
  the launcher from every non-launcher screen.

## Screen power

- Automatic screen sleep after an idle timeout.
- Wake on touch.
- Sleep should turn the backlight off, pause/limit UI rendering and keep audio,
  HA and Wi-Fi alive.
- The wake touch must not activate the tile/control underneath the finger.
- Timeout and wake brightness should be configurable and persisted.

## Lighting UX / HA model

- Color picker for lights (wheel, presets, RGB and/or color temperature as
  supported by each entity).
- Automatic layout based on the number of discovered devices; fit a responsive
  grid instead of a fixed four cards.
- Group control: multiple lights together (HA groups or device-local groups).
- Favorites/pins: choose which entities appear on the dashboard.
- Full device view: every discovered light with all available controls (color,
  brightness, maybe effects/animations — decide later).
- Decide discovery source: HA entities by domain/area/label, or an explicit
  config list.
- Keep optimistic UI updates and command coalescing; respect HA rate limits.
- Current M7: four configured light entities with on/off and brightness only.

## Open questions

1. Clock arrows: time adjustment, date navigation, or timezone picker?
2. MENU vs HOME semantics once the dashboard is home.
3. Which direct weather provider, location source and fallback order?
4. Where do light favorites live: HA labels/areas or a device-local list?
5. Do groups come from HA group entities or local groupings?
6. How much animation/effect support do the target lights expose?
