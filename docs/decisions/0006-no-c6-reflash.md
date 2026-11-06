# ADR-0006 — Do not reflash the C6 without a diagnosis

**Status:** Accepted  
**Date:** 2026-11-06

## Context

The ESP32-C6 runs the Wi-Fi/BLE radio firmware and communicates with the P4 over
SDIO using ESP-Hosted. Host and slave protocol versions must match. The factory
C6 image completed the handshake with `esp_hosted 1.4.7` and
`esp_wifi_remote 1.2.5`; no C6 update is required for the current milestone.

Reflashing the C6 is a separate recovery operation with its own toolchain,
header access and risk of leaving the board without working Wi-Fi.

## Decision

Pin the host side to the validated pair:

```yaml
espressif/esp_hosted: "1.4.*"
espressif/esp_wifi_remote: "==1.2.5"
```

Do **not** reflash the C6 unless:

1. the host boot log shows a protocol/version mismatch or transport failure;
2. the exact C6 image and ESP-Hosted version are identified;
3. a recovery plan (UART header, IO9/EN sequence, known-good image) is ready.

Capture the host log before touching the C6.

## Consequences

- Wi-Fi remains on the known-good factory image.
- Upgrading ESP-Hosted later requires a coordinated host + C6 update.
- BLE remains unvalidated until that work is deliberately scheduled.
- C6 recovery is documented as an expert operation, not a normal build step.

See [`../hardware.md`](../hardware.md).
