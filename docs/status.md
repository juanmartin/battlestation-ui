# Status

**As of:** 2026-11-06  
**Milestone:** M4 complete on `main`; M5 launcher shell implemented on
`m5-launcher`, pending hardware acceptance  
**Firmware tag:** `bringup-m1` (stable bring-up)

## Verified unit

| Property | Value | Evidence |
| --- | --- | --- |
| Board | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (SKU 32510) | Silkscreen, official docs |
| P4 silicon | **rev1.3** (`min v1.0`, `max v1.99`) | `esptool chip_id`, `00_board_check` |
| CPU | 360 MHz, dual-core HP RISC-V | Runtime log |
| Flash | 32 MB, GD, QIO | `esptool flash_id`, runtime |
| PSRAM | 32 MB, 200 MHz, X16 | Runtime log |
| Internal heap free | ~445 KB after boot | `00_board_check` |
| PSRAM free | ~33.4 MB | `00_board_check` |
| Wireless | ESP32-C6-MINI-1-N4 over SDIO | ESP-Hosted boot log |
| Serial | CH343P, Apple CDC driver, 460800 baud | Successful flash, verified hashes |

## Verified paths

| Path | State | Evidence |
| --- | --- | --- |
| EK79007 MIPI-DSI 1024×600 | Working, 180° product orientation | `08_lvgl_display_panel`, `battlestation-ui` |
| GT911 touch | Working at I2C `0x5D`, polling mode | Touch coordinates + visual dot |
| LVGL 9.5 + adapter | Working with `DOUBLE_DIRECT` at 180° | 386 s soak |
| ES7210 MIC1/MIC2 | Working, 16 kHz stereo 16-bit, 24 dB PGA | Live bars, dBFS/peak |
| ES8311 + NS4150B | Playback path initialized | `06_i2s_codec` echo test |
| C6 hosted Wi-Fi | Working, ESP-Hosted 1.4.7, SDIO 4-bit @ 40 MHz | Association + IP + ping |
| Concurrent operation | Working | 341 s combined soak |

## Stability summary

The earlier UI freeze was a **display-pipeline deadlock inside the adapter's
rotated partial-refresh path**, not a power, panel or PSRAM fault. Raw DSI ran
70 s at 66 fps with zero completion timeouts.

Current default:

```text
tear mode:  DOUBLE_DIRECT
rotation:   180°
adapter:    esp_lvgl_adapter 0.7.5 (local override)
PPA:        disabled
```

Measured results:

| Configuration | Result |
| --- | --- |
| Raw `esp_lcd` DSI, no LVGL | 70 s, 66 fps, 0 timeouts |
| LVGL, rotation 0°, `TRIPLE_PARTIAL` | 75 s stable |
| LVGL, 180°, `TRIPLE_PARTIAL` | deadlock ~21–26 s |
| LVGL, 180°, `TRIPLE_FULL` | 75 s stable (fallback) |
| LVGL, 180°, `DOUBLE_DIRECT` | **386 s stable**, ~20 fps idle / 60 fps touch |
| + Wi-Fi + 5 Hz ping + touch + audio | **341 s stable**, 1.6% ping loss, 30.9 ms avg |

Details: [`stability.md`](stability.md).

## Build output

| Profile | Binary | App slot | Free |
| --- | ---: | ---: | ---: |
| rev3.x | 1,066,112 B | 8 MB factory | ~87% |
| rev1.x overlay | 1,061,984 B | 8 MB factory | ~87% |

No filesystem, no OTA slots. Settings (when added) go to a small NVS namespace.

## Known warnings / open items

| Item | State | Action |
| --- | --- | --- |
| `ledc: GPIO 32 is not usable` warning | Known BSP double-init | Validate brightness linearity; guard second `bsp_display_brightness_init()` if needed |
| 10-minute idle soak | Pending | Next validation gate before M5 |
| PPA acceleration | Disabled | Revisit only with the PPA patch and a benchmark |
| BLE through C6 | Not validated | Separate milestone; not required for M5–M8 |
| Wi-Fi latency | 30.9 ms avg, 1.6% loss | Acceptable for HA; tune later |
| dBFS calibration | Not calibrated | Displayed value is relative digital level, not dB SPL |
| HA integration | Not started | M7 |
| Launcher / app manager | Not started | M5 |

## M5 launcher shell (branch `m5-launcher`)

Implemented in firmware and build-verified for rev1.3; hardware acceptance is
still pending.

- `app_manager` with a static table of four apps: **launcher**, **dashboard**,
  **organism** (placeholder), **settings**.
- Launcher home grid with tappable tiles and a persistent `HOME` button on every
  non-launcher screen.
- Dashboard is the existing bring-up probe, mounted/unmounted as an app.
- Settings has a live brightness slider; organism is an M8 placeholder.
- App switching is deferred to the manager timer so an LVGL event target is
  never deleted inside its own callback ([ADR-0011](decisions/0011-deferred-app-switching.md)).
- Manager owns the heartbeat/FPS counters so diagnostics stay valid across
  switches.
- Build: ~1.09 MB, 87% of the 8 MB app slot free.
- Self-test: **100 switches passed** on hardware — LVGL child count 6 → 6
  (delta 0), PSRAM delta 0, internal heap delta 484 B, no errors. Boot to
  launcher ~1.5 s.

Acceptance still to run on hardware:

- [x] Boot reaches the launcher in < 3 s (measured ~1.5 s).
- [x] 100 app switches without crash or LVGL object growth (self-test).
- [ ] Dashboard, audio, Wi-Fi and touch still behave after repeated switches
      (audio/Wi-Fi verified; touch still manual).
- [ ] Brightness slider changes the panel immediately (manual).
- [ ] 10-minute idle soak from the launcher (running).

## Next

1. Flash `m5-launcher` and run the M5 acceptance checks.
2. Close the 10-minute idle soak.
3. Start **M6 — shared audio service + analysis**.
