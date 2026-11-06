# M6 audio validation

Evidence for the shared audio service and analysis refactor. Target: rev1.3,
16 kHz stereo 16-bit, PGA 24 dB, `DOUBLE_DIRECT` display pipeline.

## Architecture

- `services/audio_service.c` — sole owner of the duplex I2S bus, ES7210/ES8311,
  capture readers, playback lanes and mixer.
- `services/audio_analysis.c` — 20 ms analysis task: RMS/dBFS/peak/Leq,
  1024-point FFT, 16 half-octave bands, feature snapshot.
- `engine/audio_ring.c` — SPSC capture/playback rings in PSRAM.
- `engine/audio_features.c` + `audio_fft.c` — pure-C DSP, host-testable.
- `engine/audio_mixer.c` — pure-C lane mixer (sum, trim, saturate).
- `engine/audio_seqlock.h` — lock-free feature snapshot.
- Playback lanes: tone (highest), metronome, notification; 80 ms rings; start
  muted; ducking per ADR-0013.

## Step 2 — capture ring + levels

180 s hardware run with Wi-Fi:

- 179 analysis updates, zero errors/stalls.
- `audio_rx` stack high-water: 1068 B free.
- `audio_analysis`: 2648 B free.
- Internal heap ~286 KB, PSRAM stable.
- Two startup timeout warnings before the first frames; fixed in Step 3.

## Step 3 — FFT, bands, Leq

Host tests:

- 1 kHz → bin 64 (1000 Hz), −3.01 dBFS, band 9.
- 4 kHz → bin 256 (4000 Hz), −3.01 dBFS, band 13.
- 100 Hz / low E 82.41 Hz → strongest band 2 (88.4 Hz).
- Silence → −120 dBFS spectrum floor.

Target timing, before optimization:

- steady ≈ 3.0–3.1 ms; cold-start max ≈ 19.6 ms.

After optimization (linear FFT power, precomputed band ranges, spread spectrum
dB conversion, libm warm-up):

- steady ≈ 1.44 ms; max ≈ 1.75 ms over 45 s.
- Spectrum dB conversion is spread over four frames; full graph refresh 80 ms.

## Step 4 — playback lanes + mixer

Normal build (Wi-Fi, output muted):

- ES7210 and ES8311 open with the same `16000/2/16` format.
- `ES8311 playback ready, volume 60%, muted`.
- `audio_tx` running; `tx` frames increasing, `under=0`, `pbr=1`, `muted=1`.
- 45 s: zero errors/stalls; `audio_tx` stack 3544 B free.

Test-tone build (`CONFIG_APP_AUDIO_TEST_TONE=y`):

- Tone lane opens at 3.5 s, 1 kHz −20 dBFS for 2 s, then 200 ms silence.
- `audio_selftest: start` / `done`; zero write-short warnings, `under=0`.
- Capture/analysis continues; no I2S or codec errors.
- Output returns to muted after the test.

## 10-minute duplex soak

Run: capture + analysis + `audio_tx` silence + Wi-Fi + launcher.

- Bar: no stall, no reset, `under=0`, heap/PSRAM stable, `dsp` max < 5 ms.
- Result: **TBD after the soak completes.**

## Host test suite

```bash
cmake -S tests/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

3/3 tests pass:

- `test_fft` — 550 checks.
- `test_features` — 89 checks.
- `test_mixer` — 1290 checks.

## Builds

| Build | Size | Free |
| --- | ---: | ---: |
| rev1.3, no Wi-Fi | 1,117,024 B | 87% |
| rev1.3 + Wi-Fi | 1,266,624 B | 85% |
| rev1.3 + test tone | 1,120,736 B | 87% |

All builds: zero compiler warnings.

## Open items

- Playback lane `close`/`flush` must not race an in-flight `write` on the same
  source; the contract is documented in `audio_service.h`. M9 app lifecycle will
  enforce “stop writer before close” or add a per-lane writer lock.
- The test tone is a codec/duplex smoke test, not an acoustic calibration.
- `dBFS` remains a digital level, not dB SPL.
