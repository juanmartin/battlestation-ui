# Progress log

Dated engineering log. One entry per milestone or significant finding; link to
the detailed document instead of duplicating it.

## 2026-11-06 — Research and stack selection

- Selected **ESP-IDF v5.5.5 + Waveshare BSP 3.0.1 + LVGL 9.5.0** as the base.
- Rejected Arduino, PlatformIO, ESP-Brookesia-as-framework, LVGL 8.x/9.2 and the
  legacy `esp_lvgl_port`.
- Recorded the verified pin map, display timing, audio routing and C6 SDIO
  wiring from the schematic/BSP.
- Decision: keep hardware knowledge behind a BSP/service boundary.
- Evidence: [`research/handoff-2026-11-06.md`](research/handoff-2026-11-06.md).

## 2026-11-06 — Environment and official examples

- Installed ESP-IDF v5.5.5 on macOS Apple Silicon.
- Built official examples `00`, `03`, `05`, `06`, `08`, `09` for `esp32p4`.
- Established that first-time builds must be serialized: parallel builds corrupt
  the shared Component Manager cache.
- Evidence: `docs/research/handoff-2026-11-06.md` §D.

## 2026-11-06 — Board identity

- Unit is **ESP32-P4 rev1.3**, not the repo default rev3.x.
- 32 MB GD QIO flash, 32 MB PSRAM at 200 MHz, 360 MHz CPU.
- CH343P enumerated with Apple's built-in CDC driver; 460800 baud flashing
  succeeded without the WCH driver.
- Decision: keep both revision profiles; this unit builds with the rev1.3
  overlay ([ADR-0005](decisions/0005-revision-profiles.md)).

## 2026-11-06 — Display and touch

- EK79007 MIPI-DSI initialized at 1024×600; GT911 found at `0x5D`.
- Verified RGB→white sequence and touch squares.
- Correct mapping at 180° is **no mirror flags**; the BSP/example combination of
  180° + mirrors maps the visual top-left to the opposite corner.
- Five-point capability exists but the bring-up keeps single-pointer semantics.

## 2026-11-06 — Audio capture

- ES7210 MIC1/MIC2 captured at 16 kHz, stereo, 16-bit, 24 dB PGA.
- Per-channel RMS/dBFS/peak implemented; confirmed both channels respond.
- Confirmed MIC3/MIC4 are the speaker/AEC reference network, not extra mics.
- Recorded that dBFS is relative digital level, not calibrated dB SPL.

## 2026-11-06 — Hosted Wi-Fi

- ESP-Hosted 1.4.7 / `esp_wifi_remote` 1.2.5 completed the handshake with the
  factory C6 image.
- SDIO 4-bit at 40 MHz, GPIO map confirmed.
- No C6 reflash required; decision recorded in
  [ADR-0006](decisions/0006-no-c6-reflash.md).

## 2026-11-06 — Display freeze root cause and fix

- Symptom: LVGL worker stops after ~20–26 s while audio, heap, PSRAM and
  temperature stay healthy; no panic or watchdog reset.
- Isolated to the adapter's rotated partial-refresh path with an unbounded
  framebuffer wait.
- Raw DSI stress proved the panel/DSI/PSRAM/power path healthy: 70 s at 66 fps,
  zero completion timeouts.
- Adopted `DOUBLE_DIRECT + 180°` with adapter 0.7.5.
- Soaks: 386 s display/audio/touch; 341 s with Wi-Fi + 5 Hz ping + touch + audio.
- Decision: [ADR-0002](decisions/0002-display-pipeline.md),
  [ADR-0003](decisions/0003-local-adapter-override.md).

## 2026-11-06 — Repository organization

- Tagged the bring-up firmware `bringup-m1`.
- Moved the research handoff into `docs/research/`, created the living handbook
  (`status`, `hardware`, `bringup`, `stability`, `architecture`, `roadmap`),
  ADRs and this progress log.
- Imported the DSI stress test as `probes/dsi/`.
- Recorded upstream provenance and added `tools/fetch_upstream.sh`; external
  sources stay out of the repository.
- Decision: [ADR-0004](decisions/0004-external-dependencies.md),
  [ADR-0007](decisions/0007-portability-boundary.md).

## 2026-11-06 — M5 launcher shell merged

- Added `app_manager` with a static app table: launcher, dashboard, organism
  placeholder, settings.
- Added a home grid, persistent HOME button, and a live brightness slider.
- Reused the bring-up probe as the dashboard app; audio/Wi-Fi still run as
  probes until M6.
- App switches are deferred out of LVGL event callbacks
  ([ADR-0011](decisions/0011-deferred-app-switching.md)).
- Manager owns heartbeat/FPS so diagnostics remain valid when the dashboard is
  unmounted.
- Build verified for rev1.3: ~1.09 MB, 87% free (1.23 MB with Wi-Fi).
- On-hardware self-test: **100 switches**, child count 6 → 6, PSRAM delta 0,
  internal heap delta 484 B, no errors. Boot to launcher ~1.5 s.
- 10-minute launcher soak: **611 s**, heartbeat 99 → 12047, zero stalls, no
  reset, PSRAM delta 0, internal heap stable at ~294 KB, 38.3 °C.
- Touch navigation verified live through organism → settings → dashboard →
  HOME. Brightness slider still needs a visual check.
- PR #1 merged to `main`.

## 2026-11-06 — Roadmap expanded: dashboard cards and tone lab

- Dashboard: clock, weather, Wi-Fi/audio status, and four Home Assistant light
  cards, each expandable to a fullscreen detail view
  ([ADR-0012](decisions/0012-dashboard-cards.md)).
- New tuner/tone-lab app: guitar/chromatic tuner, waveform generator, multiple
  graphs and metronome ([ADR-0013](decisions/0013-audio-output-tuner.md)).
- M6 scope expanded to own the duplex I2S bus and define the playback API now,
  so the tone lab does not force a second audio refactor.
- Roadmap renumbered: M9 tuner/tone lab, M10 settings, M11 stabilization.

## 2026-11-06 — M6 audio service (branch `m6-audio-service`)

- Split capture out of `audio_probe` into `audio_service` in two steps: a
  behavior-preserving refactor, then a 200 ms SPSC ring with an `audio_rx` task.
- Added `audio_analysis`: 20 ms task, legacy levels, 1 s Leq, 1024-point FFT,
  16 half-octave bands, seqlock feature snapshot.
- Added pure-C `audio_ring`, `audio_features`, `audio_fft`, `audio_mixer` and
  `audio_seqlock`; host tests cover FFT, bands, levels, Leq and mixer.
- Added duplex playback ownership: ES8311 opens with the same 16 kHz/2/16
  format, starts muted, three playback lanes and a TX mixer.
- Added an opt-in 1 kHz test tone; it plays and returns to mute with `under=0`.
- Analysis timing: 1.44 ms steady / 1.90 ms max (gate < 5 ms); early 3.1 ms
  steady and 19.6 ms cold spike were removed by linear FFT power, precomputed
  band ranges and spread spectrum dB conversion.
- 10-minute duplex soak: **615 s**, 122 samples, heartbeat 99 → 12076,
  `under=0`, PSRAM delta 0, internal heap stable at ~246 KB, 36.3 °C, `dsp`
  max 1.90 ms. Zero errors/stalls/resets.

## 2026-11-07 — Brightness verified; local CI replaces GitHub Actions

- Brightness slider verified on hardware; M5 acceptance is now complete.
- GitHub Actions is blocked by an account billing issue. Removed the workflow
  and added local CI: `make ci` runs whitespace checks, host DSP tests and the
  rev1.3 firmware build. `make ci-quick`, `make host`, `make firmware` and
  `make asan` are available.
- Optional versioned git hooks: `pre-commit` runs `--quick`, `pre-push` runs the
  full gate; install with `tools/install_git_hooks.sh`.
- Hosted alternatives and the rationale are documented in
  [`ci.md`](ci.md).

## 2026-11-09 — M7 dashboard + HA

- Replaced the probe-as-dashboard with a card grid: clock, weather, status and
  four light cards; fullscreen detail views with BACK.
- Added `time_service` (SNTP) and a real HA client: WebSocket state push + REST
  initial sync/commands, command queue, 30 s resync and auto-reconnect.
- Slider commands are coalesced to one send on release; light cards show
  pending/syncing/offline states honestly.
- Moved the old probe to the Diagnostics app, reachable from the status card.
- Added Kconfig for timezone, HA URL/token, four light entities, weather entity
  and an optional HA command self-test.
- 100-switch self-test: dashboard mounted 25 times, child 6 → 6, PSRAM delta 0,
  no errors/stalls. 30 s device smoke passed with Wi-Fi.
- Mock HA: WS auth/subscribe, four light states, weather, REST on/off commands
  and WS light/weather pushes; an initial WS error/disconnect reconnected.
- Pending: visual layout/detail check, real HA instance and the 30-minute soak.

## 2026-11-09 — M8 organism

- Implemented the pure-C organism engine: 96×96 vector field, 3072 particles,
  trails, palette, audio/touch inputs; host test 2150 checks + ASan.
- First LVGL image integration reached only ~6 fps; switched to the adapter's
  direct dummy-draw framebuffer path (ADR-0016).
- Engine pre-rotates for the panel, renders directly into the adapter free
  buffer, draws a HOME hint and flushes; no extra full-frame copy.
- Hardware: **30.2 fps**, step 20.7 ms, flush 11.4 ms, zero errors/underruns,
  no watchdog.
- 100-switch self-test: organism mounted/unmounted 25 times, child 6 → 6,
  PSRAM delta 0.
- 10-minute organism soak: **615.6 s**, 29.9–30.2 fps, step avg 19.6 ms,
  flush avg 12.6 ms, zero errors/underruns, no watchdog, PSRAM delta 0.
- Visual check and full 30-minute soak pending.

## 2026-11-09 — M8.1 organism touch + loudness density

- Fixed organism touch: dummy-draw mode has no rendered object tree, so the
  overlay events were unreliable. A 16 ms LVGL timer now polls the pointer
  indev (`lv_indev_get_state`/`lv_indev_get_point`) and publishes state to the
  engine task.
- Added a soft fingertip glow, expanding ripple and gentle swirl; reduced the
  burst impulse after the first feel check.
- Brightness is fixed. Loudness now controls the active particle pool
  (~1,024 quiet → 4,096 loud) and loud frames use 3×3 cores, so sound visibly
  fills the scene.
- Full-density hardware: **30.2 fps**, step ~22.6 ms, flush ~9.5 ms, zero
  errors/watchdog. Startup/remount windows briefly show ~25.5 fps while LVGL
  rebinds the panel.
- Merged PR #5 and tagged **`m8.1`**; normal launcher + Wi-Fi build restored.
- Host tests 4/4; zero compiler warnings; firmware 1,430,976 B (83% free).
- Full 30-minute soak at 4,096 particles still pending.

## 2026-11-10 — M9 tuner + tone lab

- Added pure-C YIN tuner, phase-continuous tone generator and Q32.32 metronome;
  host tests 7/7 (tuner ±0.01 cents synthetic, tone 0.1%, metronome
  sub-sample). ASan/UBSan clean.
- Added `tuner_service`: capture reader + tuner task, tone/metronome generator
  task and two playback lanes. Tone/metronome lanes are only active when
  enabled so an idle silent lane cannot duck the other.
- Added the Tuner app: pitch/cents/needle/waveform, tone controls, metronome
  controls/visual beat, header output-volume slider.
- Fixed two hardware issues found in the feel pass: metronome was silent because
  the service zeroed the engine gain fields; sweep now ends in silence instead
  of parking at the final frequency.
- Added a -50 dBFS RMS gate so room rumble does not invent low notes.
- Hardware: 440 Hz tone read back acoustically as 440.1 Hz / +0.2 cents; tone
  and metronome audible; volume works; zero TX underruns; no watchdog/reset.
- Lifecycle: 5 apps × 25 cycles = 125 switches, tuner mounted/unmounted 25
  times; child delta 0, PSRAM delta 0, no errors.
- Merged PR #6 and tagged **`m9`**; normal launcher + Wi-Fi build flashed and
  device smoke passed.
- Pending: 30-minute soak, real low-E guitar check.

## Next

- Merge/tag M9; restore the normal launcher build.
- M10 settings/provisioning, then M11 stabilization.
- Deferred: full 30-minute organism soak at 4,096 particles and the M9 soak.
- Post-M11 ideas: [`backlog.md`](backlog.md).
