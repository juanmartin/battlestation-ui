# ADR-0009 — No filesystem or OTA in the bring-up firmware

**Status:** Accepted for the bring-up milestone; revisit for a product release  
**Date:** 2026-11-06

## Context

The current image has no user content to persist. Assets can be compiled into
the app; device settings are small key/value pairs. SD cards and filesystems add
failure modes, mount latency and a larger attack surface. OTA doubles the app
partition requirement and needs a recovery story.

## Decision

- Use an **8 MB factory app partition**, no SPIFFS/LittleFS, no SD card.
- Keep device settings in a **small NVS namespace** only:
  `wifi_ssid`, `wifi_pass`, `brightness`, `mic_gain`, `ha_url`, `ha_token`.
- Do not add OTA slots in the bring-up image.

Partition layout:

```text
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0xa000,   0x6000
phy_init, data, phy,     0x10000,  0x1000
factory,  app,  factory, 0x20000,  8M
```

## Consequences

- Simple boot and update path: reflash over USB.
- Settings survive reboot; no history/logs/media are stored.
- Adding OTA later requires repartitioning and a full reflash, not an OTA.
- Two 8 MB app slots fit in 32 MB flash if OTA becomes a requirement.

See [`../architecture.md`](../architecture.md) and
[`../roadmap.md`](../roadmap.md).
