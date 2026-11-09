# M8 organism validation

Target: rev1.3, 1024×600, 3072 particles, direct adapter framebuffer path.

## Engine

- Pure C, no LVGL/ESP-IDF includes.
- 96×96 vector field, 16-byte particles, cheap RGB565 trail fade, precomputed
  8×64 palette.
- Audio mapping: bass → speed, mid → visible fraction, treble → hue, level →
  brightness. Touch is an attractor, repelled on strong bass.
- Host test: 300 frames, determinism, rotation, non-black/changing buffers,
  NaN/Inf inputs, invalid configs; 2150 checks.
- Full-res state: ~85 KB PSRAM for 4096 particles (3072 used), no per-frame
  allocations.

## Path comparison

| Path | Result |
| --- | --- |
| LVGL full-screen image | ~6 fps display; engine step 43–54 ms |
| Direct dummy draw, CPU-rotated copy | 12 fps; step 40 ms + copy 37 ms |
| Direct dummy draw, engine pre-rotated | **30.2 fps**; step 20.7 ms + flush 11.4 ms |

The final path eliminates the engine-owned 3.7 MB canvas buffers and the extra
1.2 MB copy. The engine renders directly into the adapter's free buffer;
`rotate_180` is applied in the particle plot.

## Hardware

- 3072 particles: **30.2 fps**, step ~20.7 ms, flush ~11.4 ms, 0 errors,
  0 audio TX underruns, no task watchdog.
- LVGL stack high-water 3.8 KB free; internal heap ~237 KB; PSRAM ~28.5 MB.
- 100-switch self-test: organism mounted/unmounted 25 times, launcher child
  count 6 → 6, PSRAM delta 0, no errors/stalls.
- 30 s device smoke: launcher/audio/Wi-Fi pass.
- Normal board image boots to launcher with the organism available from its
  tile.

## 30-minute soak

Run: organism active, Wi-Fi, audio, 3072 particles.

- Bar: ≥30 fps, no watchdog/reset, no frame errors, heap/PSRAM stable.
- Result: **TBD after the soak completes.**

## Builds

| Build | Size | Free |
| --- | ---: | ---: |
| rev1.3, no Wi-Fi | 1,336,336 B | 84% |
| rev1.3 + Wi-Fi, boot organism | 1,426,176 B | 83% |
| rev1.3 + switch self-test | 1,337,744 B | 84% |

Zero compiler warnings. Host tests 4/4; ASan/UBSan clean.

## Pending / known limits

- Visual check: orientation, HOME hint/hotspot, touch response.
- Full 30-minute soak.
- Dummy draw is global display state; enable/disable is handled on mount/unmount.
- Only 0°/180° rotation is supported by the pre-rotation path.
- The HOME hint is drawn by the engine; the LVGL HOME button is not rendered in
  dummy-draw mode.
- PPA acceleration remains disabled.
