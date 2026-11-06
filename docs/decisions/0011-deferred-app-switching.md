# ADR-0011 — Deferred app switching from LVGL events

**Status:** Accepted  
**Date:** 2026-11-06

## Context

Switching apps deletes the current LVGL tree. A tile or HOME button click runs
inside the clicked object's event callback; deleting that object (and its event
target) during the callback risks a use-after-free and corrupts LVGL's event
list.

## Decision

UI event callbacks call `app_manager_request(id)`, which only records the target
app. The app-manager timer (50 ms) performs the actual unmount/clean/mount on the
LVGL task after the event callback has returned.

`app_manager_switch(id)` remains available for non-UI callers that can take the
LVGL lock directly.

The manager also owns the heartbeat and FPS counters, so diagnostics remain
valid when the dashboard is unmounted.

## Consequences

- App switches have up to 50 ms of latency; imperceptible for a launcher.
- The event target is never deleted from inside its own callback.
- One manager timer must exist for switching to work.
- App `unmount()` must release timers and non-LVGL resources; the manager deletes
  the LVGL tree afterwards.

See [`../architecture.md`](../architecture.md) and
[`../../main/app_manager.c`](../../main/app_manager.c).
