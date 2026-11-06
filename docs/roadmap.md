# Roadmap

Each milestone has a concrete acceptance gate. Do not skip the hardware
validation gates.

| Milestone | Status | Gate |
| --- | --- | --- |
| M0 — Environment and board identity | **Done** 2026-11-06 | IDF 5.5.5, revision, flash, PSRAM |
| M1 — Display and touch | **Done** 2026-11-06 | Correct panel, orientation, touch mapping |
| M2 — Audio capture and playback | **Done** 2026-11-06 | Stereo capture, echo path, gain |
| M3 — Hosted Wi-Fi | **Done** 2026-11-06 | C6 handshake, IP, ping |
| M4 — Integrated probe | **Done** 2026-11-06 | All paths concurrent, soaks |
| M5 — Launcher shell | **Done** (manual UI check pending) | App switching, no leak |
| M6 — Shared audio service + analysis | Planned | Analysis < 5 ms/frame |
| M7 — Dashboard + Home Assistant | Planned | HA control + state push |
| M8 — Generative organism | Planned | ≥ 30 fps, no UI stall |
| M9 — Settings and provisioning | Planned | Persistence, bad input handling |
| M10 — Stabilization | Planned | 24 h soak, graceful degradation |

## M0 — Environment and board identity

- Install IDF v5.5.5, build `00_board_check`.
- Read `esptool chip_id`, `flash_id`, board silkscreen.
- **Accept:** correct target, revision, 32 MB flash, PSRAM initialized, no boot
  loop.
- **Result:** P4 rev1.3, 32 MB GD QIO, 32 MB PSRAM, 360 MHz.

## M1 — Display and touch

- Flash `08_lvgl_display_panel`, then the integrated probe.
- **Accept:** RGB screens, white canvas, black square under the finger, all
  corners reachable, 10-minute idle stable.
- **Result:** EK79007 + GT911 `0x5D`; 180° uses no mirror flags. The 10-minute
  idle soak remains an open gate.

## M2 — Audio capture and playback

- Flash `06_i2s_codec` in echo mode; speaker on PH2.0.
- **Accept:** clear mic-to-speaker echo; no I2C/I2S errors; both MIC1/MIC2
  respond when covered individually; measure noise floor at 24 dB.
- **Result:** ES7210 stereo capture and ES8311/NS4150B path verified.

## M3 — Hosted Wi-Fi

- Flash `05_wifistation` with 2.4 GHz credentials.
- **Accept:** ESP-Hosted handshake, IP, ping/HTTP to LAN, reconnect after AP
  reboot, no C6 reflash.
- **Result:** ESP-Hosted 1.4.7, SDIO 4-bit @ 40 MHz, factory C6 image.

## M4 — Integrated probe

- Flash `battlestation-ui`.
- **Accept:** display + touch + mic bars + Wi-Fi live simultaneously; FPS stable;
  10-minute soak; no watchdog; heap/PSRAM stable.
- **Result:** 386 s display/audio/touch soak, 341 s combined Wi-Fi/ping/touch/
  audio soak, zero UI stalls. 10-minute idle soak still to close before M5.

FPS note: idle ~20 fps is normal because labels update at 20 Hz; continuous
touch raises it to ~60 fps. That is not a rendering ceiling.

## M5 — Launcher shell

- Implement `app_manager`, three static app descriptors, home grid, settings
  placeholder.
- **Accept:** 100 app switches with no crash/leak; LVGL object count returns to
  baseline; brightness works; boot to launcher < 3 s.
- **Implemented on `m5-launcher`:** `app_manager`, four static apps (launcher,
  dashboard, organism placeholder, settings), home grid, persistent HOME button,
  live brightness slider. PR #1.
- **Self-test result:** 100 switches, child count 6 → 6, PSRAM delta 0,
  internal heap delta 484 B, no errors. Boot to launcher ~1.5 s.
- **Soak result:** 611 s, heartbeat 99 → 12047, zero stalls, no reset, PSRAM
  delta 0, internal heap stable at ~294 KB.
- **Pending:** manual touch-through-switches and brightness slider check; then
  merge to `main`.

## M6 — Shared audio service + analysis

- Move capture into `audio_service`; add ring buffer, 20 ms cadence, RMS/dBFS,
  peak/Leq, 1024-point FFT, 16 bands.
- **Accept:** 16 kHz stereo; analysis < 5 ms/frame; UI unaffected; known tones
  produce expected bins; dBFS tracks a signal generator monotonically.

## M7 — Dashboard + Home Assistant

- Rolling 15 s level graph, light on/off/brightness, connection status.
- **Accept:** HA light toggles < 500 ms on LAN; state changes < 1 s; graph
  ≥ 30 fps; reconnect after Wi-Fi drop.

## M8 — Generative organism

- Independent engine, double-buffered 1024×600 canvas, particles/field/trails,
  touch + audio inputs.
- **Accept:** ≥ 30 fps at target particle count; touch response < 50 ms; 30-min
  soak; no UI stall; engine builds/tests on macOS without LVGL.

## M9 — Settings and provisioning

- Brightness, mic gain, Wi-Fi scan/connect, HA URL/token; NVS persistence.
- **Accept:** settings survive reboot; invalid SSID/token handled without crash;
  no filesystem/logs created.

## M10 — Stabilization

- 24-hour soak with Wi-Fi/HA/audio/organism; watchdog; error injection (AP off,
  HA off, unplug speaker).
- **Accept:** no reboot/leak; heap/PSRAM high-water documented; graceful
  degradation; final memory/performance report.
