# M9 tuner + tone lab validation

Target: rev1.3, 1024×600, 16 kHz capture/playback, ES7210 in / ES8311 out.

## Engines

All engines are pure C, LVGL/ESP-IDF-free and host-tested.

- **Tuner** — YIN difference function, cumulative-mean normalized difference,
  absolute threshold, parabolic interpolation and a golden-section fractional
  lag refinement. 2048-sample window at 16 kHz (128 ms), 800-sample hop
  (~20 Hz). Reports frequency, MIDI note, note name/octave, cents, confidence.
- **Tone** — phase-continuous Q32 oscillator; sine wavetable plus square,
  triangle and saw; linear/log sweep; 5 ms amplitude/frequency ramps and
  waveform crossfade; stereo or mono render.
- **Metronome** — Q32.32 sample clock, 30–240 BPM, 2/4, 3/4, 4/4, 6/8,
  configurable accent, sine/square click with 1 ms attack and squared decay.
  BPM changes apply at the next beat; clicks are 30 ms by default.

Memory: tuner ~14 KB (16 kHz/2048), tone 2,168 B caller-owned, metronome 120 B.
No allocation in render/process paths.

## Host tests

7/7 ctest pass:

| Test | Checks |
| --- | ---: |
| `test_tuner` | 106 |
| `test_tone` | 4,057 |
| `test_metronome` | 33,182 |

Tuner accuracy on synthetic input:

| Input | Result |
| --- | --- |
| E2, 16 kHz | +0.0001 cents |
| A4, 16 kHz | +0.0074 cents |
| E2, 48 kHz | -0.0002 cents |
| A4, 48 kHz | +0.0004 cents |
| Saw A2 | -0.0005 cents |
| Square G3 | +0.71 cents |
| Square A4 | +0.40 cents |

ASan/UBSan run: all 7 tests pass.

## App and service

- `tuner_service` owns one capture reader, the tuner engine, tone/metronome
  engines and two playback lanes (`AUDIO_PRIO_TONE`, `AUDIO_PRIO_METRONOME`).
- Tuner task on core 1, generator task on core 0, both stopped on app unmount.
- Tuner app has TUNER / TONE / METRONOME tabs, cents needle, input waveform,
  tone waveform/frequency/amplitude/sweep, metronome BPM/meter/accent/click and
  visual beat.
- Header **OUTPUT VOLUME** slider controls the ES8311 output; tone and
  metronome both follow it.
- Sweep is one-shot: pressing it starts the tone, and the app disables the tone
  when the 5 s ramp ends (silence, not parked at the final frequency).
- Tone/metronome lanes are only active when enabled, so an inactive silent lane
  cannot duck the other lane in the audio mixer.

## Hardware

- Tuner: tracked spoken/hummed notes; A4 tone through the speaker read back as
  **440.1 Hz, +0.2 cents, 99%** (acoustic loopback), F3 voice ~±10 cents.
- Quiet-room run after adding a -50 dBFS RMS gate: no random low-note reports;
  humming/guitar still tracks cleanly.
- Tone: audible through ES8311; waveform/frequency/amplitude changes work; sweep
  ends silent.
- Metronome: click audible after fixing the zero-gain service config; visual
  beat/meter/accent work.
- Volume slider affects both tone and metronome.
- Audio TX underruns: **0**; no watchdog/reset during the test runs.
- Lifecycle self-test (5 apps × 25 cycles = **125 switches**, tuner started and
  stopped 25 times): child count 7 → 7 (delta 0), PSRAM delta 0, internal heap
  delta -5.9 KB, no errors/stalls. Tuner mount/unmount adds ~0.1–0.3 s of LVGL
  render time per switch in the stress loop.
- Note: the metronome initially appeared silent because the service zeroed the
  engine's gain fields; explicit defaults fixed it.

## Builds

| Build | Size | Free |
| --- | ---: | ---: |
| rev1.3, no Wi-Fi | 1,377,632 B | 84% |
| rev1.3 + Wi-Fi, boot tuner | 1,467,328 B | 83% |

Zero compiler warnings; host tests 7/7.

## Pending / limits

- Full 30-minute tuner + tone + metronome soak.
- Real guitar low-E accuracy on hardware (host ±0.0001 cents; voice tests are
  not a substitute).
- Metronome jitter on hardware (host scheduling is sub-sample).
- Volume is cached by `audio_service` at runtime; NVS persistence arrives in M10.
- 48 kHz tuner path is supported but costs ~5× the 16 kHz YIN work; the app uses
  16 kHz.
