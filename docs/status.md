# Status

**As of:** 2026-11-09  
**Milestone:** M8 organism implemented; 10-minute soak passed, visual/30-minute
soak pending  
**Firmware tag:** `bringup-m1` (stable bring-up), `m6` (audio), `m7` (dashboard/HA)

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
| 10-minute launcher soak | **Passed** 2026-11-06 | 611 s, zero stalls, no reset |
| PPA acceleration | Disabled | Revisit only with the PPA patch and a benchmark |
| BLE through C6 | Not validated | Separate milestone; not required for M5–M8 |
| Wi-Fi latency | 30.9 ms avg, 1.6% loss | Acceptable for HA; tune later |
| dBFS calibration | Not calibrated | Displayed value is relative digital level, not dB SPL |
| HA integration | Not started | M7 |
| Launcher / app manager | **Done**, merged | Touch and brightness verified 2026-11-07 |
| M6 audio service | **Done**, merged | — |
| M7a dashboard shell | Branch `m7-dashboard` | Visual check |
| M7b HA transport/weather | Mock-validated | Real HA + 30-min soak |
| M8 organism | Branch `m8-organism` | Visual + 30-min soak |

## M5 launcher shell

Merged to `main`; touch and brightness verified on hardware (2026-11-07).

- `app_manager` with a static table of four apps: **launcher**, **dashboard**,
  **organism** (placeholder), **settings**.
- Launcher home grid with tappable tiles and a persistent `HOME` button on every
  non-launcher screen.
- Dashboard is the bring-up probe, mounted/unmounted as an app. M7 expands it
  into a card dashboard (clock, weather, status, four HA lights).
- Settings has a live brightness slider; organism is an M8 placeholder.
- App switching is deferred to the manager timer so an LVGL event target is
  never deleted inside its own callback ([ADR-0011](decisions/0011-deferred-app-switching.md)).
- Manager owns the heartbeat/FPS counters so diagnostics stay valid across
  switches.
- Build: ~1.09 MB without Wi-Fi, ~1.23 MB with Wi-Fi; 85–87% of the 8 MB app
  slot free.
- Self-test: **100 switches passed** on hardware — LVGL child count 6 → 6
  (delta 0), PSRAM delta 0, internal heap delta 484 B, no errors. Boot to
  launcher ~1.5 s.
- 10-minute idle soak: **611 s**, heartbeat 99 → 12047 (zero stalls), no reset,
  PSRAM delta 0, internal heap settled at ~294 KB, temperature 38.3 °C.

Acceptance on hardware:

- [x] Boot reaches the launcher in < 3 s (measured ~1.5 s).
- [x] 100 app switches without crash or LVGL object growth (self-test).
- [x] 10-minute idle soak from the launcher.
- [x] Audio and Wi-Fi continue running across app switches.
- [x] Touch still responds after repeated switches (manual).
- [x] Brightness slider changes the panel immediately (manual, 2026-11-07).

## M6 audio service (merged)

Implemented, validated and merged to `main`.

- `audio_service` owns the duplex I2S bus, ES7210 capture, ES8311 playback,
  capture readers and three playback lanes with a pure-C mixer.
- `audio_analysis` runs at 20 ms: legacy RMS/dBFS/peak, 1 s Leq, 1024-point
  FFT, 16 half-octave bands, seqlock feature snapshot.
- Capture refactor is behavior-preserving; M5 UI is unchanged.
- Analysis time: **1.44 ms steady / 1.90 ms max** (gate < 5 ms).
- Test tone: 1 kHz −20 dBFS for 2 s, then muted; zero write errors and
  `under=0`.
- Normal build: speaker opens with the same 16 kHz/2/16 format, starts muted,
  `audio_tx` writes silence; capture/analysis unaffected.
- Host tests: 3/3 (`test_fft`, `test_features`, `test_mixer`).
- 10-minute duplex soak: **passed** — 615 s, heartbeat 99 → 12076,
  `under=0`, PSRAM delta 0, internal heap stable at ~246 KB, 36.3 °C,
  `dsp` max 1.90 ms (steady max 1.45 ms). Zero errors/stalls/resets.
- Open item for M9: playback `close`/`flush` must not race an in-flight
  `write`; app lifecycle will stop the writer first or add a per-lane lock.

Detail: [`m6-audio-validation.md`](m6-audio-validation.md).

## M7 dashboard + Home Assistant (branch `m7-dashboard`)

Implemented and mock-validated; visual check, real HA and a 30-minute soak
remain.

- Card grid: clock, weather, status and four light cards.
- Fullscreen detail view per card; BACK returns; dashboard stays mounted.
- Four light cards with on/off and brightness; slider sends one command on
  release, not per drag tick.
- HA WebSocket state push + REST initial sync/commands; 30 s resync and
  auto-reconnect; token never logged.
- Clock via SNTP, Kconfig timezone; “not synced” until a valid time.
- Old bring-up probe is now the Diagnostics app, opened from the status card.
- 100-switch self-test: dashboard mounted/unmounted 25 times, child 6 → 6,
  PSRAM delta 0, no errors/stalls.
- Mock HA: WS auth/subscribe, four light states, weather, REST on/off commands,
  WS light/weather pushes; initial WS error/disconnect reconnected.
- Device smoke: launcher/audio/Wi-Fi pass, zero errors.
- Detail: [`m7-dashboard-validation.md`](m7-dashboard-validation.md).

## M8 organism (branch `m8-organism`)

Implemented and running at 30 fps; 30-minute soak and visual check remain.

- Pure-C engine: 96×96 field, 3072 particles, trails, audio/touch inputs.
- Direct adapter framebuffer path (dummy draw): no LVGL image, no extra
  full-frame copy; engine pre-rotated for the panel.
- Hardware: **30.2 fps**, step ~20.7 ms, flush ~11.4 ms, zero errors/underruns,
  no task watchdog.
- 100-switch self-test: organism mounted/unmounted 25 times, child count 6 → 6,
  PSRAM delta 0.
- 10-minute soak: **615.6 s**, 29.9–30.2 fps, step avg 19.6 ms, flush avg
  12.6 ms, zero errors/underruns, no watchdog; PSRAM delta 0 and heap stable.
- Host tests: 4/4; organism test 2150 checks; ASan/UBSan clean.
- HOME hint + top-right hotspot; dummy draw enable/disable on mount/unmount.
- Detail: [`m8-organism-validation.md`](m8-organism-validation.md).

## Next

1. Visual check (orientation, HOME hint, touch) and the full 30-minute soak.
2. Merge M8/tag; then M9 — tuner + tone lab.
