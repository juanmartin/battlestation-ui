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
| M5 — Launcher shell | **Done**, merged | App switching, no leak |
| M6 — Shared audio service + analysis | **Done** 2026-11-06 | Analysis < 5 ms/frame |
| M7 — Dashboard + Home Assistant | **In progress** | 4 lights, clock, weather, detail views |
| M8 — Generative organism | **In progress** | ≥ 30 fps, no UI stall |
| M9 — Tuner + tone lab | Planned | Tuning accuracy, clean output |
| M10 — Settings and provisioning | Planned | Persistence, bad input handling |
| M11 — Stabilization | Planned | 24 h soak, graceful degradation |

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
  audio soak, zero UI stalls. 10-minute launcher soak passed at M5.

FPS note: idle ~20 fps is normal because labels update at 20 Hz; continuous
touch raises it to ~60 fps. That is not a rendering ceiling.

## M5 — Launcher shell

- Implement `app_manager`, static app descriptors, home grid, settings
  placeholder.
- **Accept:** 100 app switches with no crash/leak; LVGL object count returns to
  baseline; brightness works; boot to launcher < 3 s.
- **Result:** merged to `main`. 100 switches with child count 6 → 6, PSRAM delta
  0, internal heap delta 484 B; 611 s launcher soak with zero stalls; boot
  ~1.5 s. Touch and brightness verified on hardware 2026-11-07.

## M6 — Shared audio service + analysis

- First a pure refactor: move ES7210 capture out of the probe into
  `audio_service` with identical dBFS behavior.
- Then add the ring buffer, 20 ms cadence, RMS/dBFS, peak/Leq, 1024-point FFT
  and 16 bands.
- **Own the duplex I2S bus now.** Define the playback API and output mixer even
  though the first implementation only captures; M9's tone generator and
  metronome must not open the codec themselves.
- **Accept:** 16 kHz stereo; analysis < 5 ms/frame; UI unaffected; known tones
  produce expected bins; dBFS tracks a signal generator monotonically; playback
  API smoke test (silence or test tone) does not disturb capture.
- **Implemented on `m6-audio-service`:** capture refactor, 200 ms ring, 20 ms
  analysis, FFT/bands/Leq, duplex playback lanes + mixer, optional test tone.
- **Result:** analysis 1.44 ms steady / 1.90 ms max; 180 s capture run and 615 s
  duplex soak with zero errors/stalls; test tone `under=0`; host tests 3/3;
  PSRAM delta 0 and internal heap stable at ~246 KB in the soak.
- **Note:** keep capture and playback in the same service but in separate tasks
  or a clearly bounded mixer so DMA deadlines are never missed.

## M7 — Dashboard + Home Assistant

The dashboard is a grid of cards; tapping a card opens a fullscreen detail view
with more information or controls. Back returns to the grid
([ADR-0012](decisions/0012-dashboard-cards.md)). Data sources and HA
configuration are defined in
[ADR-0015](decisions/0015-dashboard-data-sources.md).

**M7a — shell**
- Card grid, detail views, clock/SNTP, Wi-Fi/audio status.
- Four light cards with on/off + brightness controls.
- HA client stubbed: “not configured/offline” state, no fake data.
- **Result:** implemented; dashboard mounted/unmounted 25 times in the
  100-switch self-test with child delta 0 and no errors/stalls; 30 s device
  smoke passed. Visual check pending.

**M7b — integration**
- HA WebSocket + REST client, state sync and four light entities.
- Weather from an HA weather entity.
- Reconnect after Wi-Fi/HA drop; 30-minute soak.
- **Result:** WS auth/subscribe, four light states, weather, REST on/off
  commands and WS pushes validated against a local mock HA. Real HA, visual
  check and the 30-minute soak remain.

- **Accept:** clock and weather update; four lights reflect HA state and command
  in < 500 ms; state push < 1 s; detail open/close < 100 ms; dashboard ≥ 30 fps;
  reconnect after Wi-Fi or HA drop; 30-minute soak.

## M8 — Generative organism

- Independent engine, double-buffered 1024×600 canvas, particles/field/trails,
  touch + audio inputs.
- **Accept:** ≥ 30 fps at target particle count; touch response < 50 ms; 30-min
  soak; no UI stall; engine builds/tests on macOS without LVGL.
- **Result:** direct adapter framebuffer path (ADR-0016), 3072 particles,
  **30.2 fps**, step 20.7 ms; 100-switch self-test with 25 organism mounts and
  zero leaks. 10-minute soak: 615.6 s, 29.9–30.2 fps, zero errors/underruns,
  stable heap/PSRAM. Visual check and full 30-minute soak pending.

## M9 — Tuner + tone lab

One app with three related tools; all audio goes through `audio_service`
([ADR-0013](decisions/0013-audio-output-tuner.md)).

- **Guitar tuner** — standard E2 A2 D3 G3 B3 E4 plus chromatic mode; note name,
  cents deviation, reference A4 (default 440 Hz). Use YIN/autocorrelation with
  parabolic interpolation on the capture stream. Validate low-E accuracy; if
  16 kHz capture is insufficient, add a dedicated higher-rate capture path
  rather than changing the shared format silently.
- **Tone generator** — sine, square, triangle and saw; frequency, amplitude and
  optional sweep; click-free ramps; output through ES8311.
- **Graphs** — time-domain waveform, spectrum/FFT and a cents needle, updated at
  ≥ 30 fps.
- **Metronome** — 30–240 BPM, 2/4, 3/4, 4/4 and 6/8, accent beat, visual beat and
  click. Share the output mixer with the tone generator with a clear priority
  rule.
- **Accept:** tuner within ±1 cent at A4 and ±3 cents at low E with a clean
  input; tone frequency within 0.1%; metronome jitter < 5 ms; no clicks or
  dropouts while capture/analysis run; 30-minute soak.
- **Result:** pure-C YIN tuner, phase-continuous tone generator and Q32.32
  metronome; host tests 7/7 (tuner ±0.01 cents on synthetic A4/E2, tone 0.1%,
  metronome sub-sample). Tuner app with three tabs, cents needle, waveform,
  volume, one-shot sweep. Hardware: A4 acoustic loopback 440.1 Hz / +0.2 cents;
  tone and metronome audible, zero TX underruns. 30-minute soak and real
  guitar low-E check remain. Detail:
  [`m9-tuner-validation.md`](m9-tuner-validation.md).

## M10 — Settings and provisioning

- Brightness, mic gain, Wi-Fi scan/connect, HA URL/token, timezone, A4 reference,
  four light entity IDs and weather entity; NVS persistence.
- **Accept:** settings survive reboot; invalid SSID/token handled without crash;
  no filesystem/logs created; dashboard and tuner pick up changes without a
  reboot where practical.

## M11 — Stabilization

- 24-hour soak with dashboard/HA/organism/tuner/tone/metronome active; watchdog;
  error injection (AP off, HA off, invalid token, unplug speaker).
- **Accept:** no reboot/leak; heap/PSRAM high-water documented; graceful
  degradation; final memory/performance report.

## Post-M11 backlog

Unscheduled product ideas captured for after the current plan. Details and open
questions: [`backlog.md`](backlog.md).

- English / Castellano localization.
- Clock arrows on the right, UTC-3 default and/or a timezone picker.
- Weather beyond the Home Assistant weather entity (direct provider option).
- Dashboard as home; MENU button for the app grid/current launcher home.
- Automatic screen sleep, wake on touch.
- Lighting UX: color picker, auto-fitting device layout, groups, dashboard
  favorites, and a full device view with all controls.

