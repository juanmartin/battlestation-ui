# ADR-0005 — Keep rev1.x and rev3.x build profiles

**Status:** Accepted  
**Date:** 2026-11-06

## Context

ESP32-P4 silicon revisions v1.x and v3.x are not boot-compatible. The Waveshare
repository defaults to rev3.x, but this unit is **rev1.3**. Flashing a mismatched
image fails in the bootloader.

## Decision

Keep both profiles in the repository:

- `sdkconfig.defaults` — rev3.x default.
- `sdkconfig.rev1_3.defaults` — overlay for rev1.x silicon.

Document the selection step before flashing and always read the chip first:

```bash
esptool.py --chip esp32p4 -p /dev/cu.usbmodemXXXX chip_id
```

For this unit:

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults" \
       -D SDKCONFIG="$PWD/sdkconfig.rev1_3" build
```

## Consequences

- One repository supports both board batches.
- The flash procedure has a mandatory identity check; do not skip it.
- Release artifacts should be labelled with the target silicon range.
- A future board port may add another overlay rather than changing the default.

See [`../bringup.md`](../bringup.md) and [`../hardware.md`](../hardware.md).
