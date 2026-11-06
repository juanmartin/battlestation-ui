# ADR-0001 — One firmware, static app table, shared services

**Status:** Accepted  
**Date:** 2026-11-06

## Context

The product has three closely coupled experiences — launcher, dashboard and
generative organism — running on one display, one audio pipeline and one network
connection. Dynamic app loading, a filesystem and per-app hardware ownership
would add memory pressure and lifecycle bugs without a product need.

## Decision

Build **one firmware image** with a static table of apps. Keep hardware resources
under single-owner services:

- `display_service` owns the BSP display, touch, LVGL lock and brightness.
- `audio_service` owns I2S and the ES7210/ES8311 codecs.
- `net_service` owns Wi-Fi/netif.
- `settings_service` owns the small NVS namespace.

Apps mount and unmount LVGL trees; they never initialize hardware. The audio
analysis task publishes a small feature snapshot that apps read without blocking
the producer.

## Consequences

- Simple, deterministic boot and memory layout; no app-store or dynamic loader.
- App switching must clean up LVGL objects correctly (M5 acceptance test).
- Background audio/network continue while an app is unmounted.
- Resource ownership must be enforced by API design, not convention.
- A future port replaces service backends, not the app layer.

See [`../architecture.md`](../architecture.md).
