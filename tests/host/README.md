# Host-side DSP tests

These tests build and run the pure-C M6 audio analysis code from `main/engine`
on the development host. They do not require ESP-IDF, FreeRTOS, LVGL or audio
hardware.

## Build and run

From the worktree root:

```bash
cmake -S tests/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

To run a single binary directly:

```bash
./build-host/test_fft
./build-host/test_features
```

`build-host/` is ignored by the repository's `build*/` rule.

## Coverage

- `test_fft` — 1024-point Hann FFT peak bins and levels for 1 kHz and 4 kHz
  sines, the 16 half-octave bands (low E / 100 Hz land in the 88.4 Hz band),
  and the -120 dBFS silence floor.
- `test_features` — -96 dBFS silence floor, full-scale RMS level, per-channel
  independence, 0.75/0.25 smoothing trajectory, 0.05 dB per 10 ms peak-hold
  decay, 1 s sliding Leq, and the 160-frame block / 1024-sample FFT counters.

## Note on peak hold

`peak_dbfs` is a **sample-peak** meter, not an RMS meter. A full-scale sine
(peak sample 32767) reads about 0 dBFS peak while its RMS level is -3.01 dBFS.
`test_peak_hold_three_dbfs_sample_peak` uses a -3.01 dBFS sample-peak sine to
cover the peak-hold magnitude requested in the M6 test brief.
