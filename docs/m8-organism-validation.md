# M8 organism validation

Target: rev1.3, 1024×600, 3072 particles, direct adapter framebuffer path.

## Engine

- Pure C, no LVGL/ESP-IDF includes.
- 96×96 vector field, 16-byte particles, cheap RGB565 trail fade, precomputed
  8×64 palette.
- Audio mapping: bass → speed, mid → flow, treble → hue, level → visible
  particle density (brightness is fixed). Touch is a gentle swirl, repelled on
  strong bass.
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

## 10-minute soak

Run: organism active, Wi-Fi, audio, 3072 particles.

- Result: **passed** — 615.6 s, 122 samples, **29.9–30.2 fps** (avg 30.2),
  step avg 19.6 ms / max 24.4 ms, flush avg 12.6 ms / max 13.0 ms, zero frame
  errors, zero audio TX underruns, PSRAM delta 0, internal heap 236–237 KB,
  39–40.3 °C, no watchdog or reset.
- The full 30-minute soak is still pending; the 10-minute run is the current
  evidence.

## Touch reaction

The direct framebuffer path bypasses LVGL rendering, so the organism no longer
relies on overlay object events. A 16 ms LVGL timer polls the pointer indev
(`lv_indev_get_state()` / `lv_indev_get_point()`) and publishes the point and
pressed state to the engine task. Pressing triggers:

- a soft palette glow under the fingertip,
- an expanding double ripple/shockwave that fades over ~0.9 s,
- a gentle vortex/swirl of nearby particles,
- local brightening and wider particle cores near the finger.

The logical top-right hotspot still returns to the launcher. Bass still repels
the touch swirl on strong beats. The ripple/burst impulse was reduced after the
first feel check: it now gives a soft shove and a stronger swirl instead of
scattering the field and leaving a particle hole.

Hardware check: real points were logged across the panel (e.g. `555,331`,
`85,372`, `880,391`, `192,52`) while the renderer held **30.2 fps** (step
20.3–21.7 ms, flush 10.5–11.8 ms), with zero errors/watchdog.

## Builds

| Build | Size | Free |
| --- | ---: | ---: |
| rev1.3, no Wi-Fi | 1,336,336 B | 84% |
| rev1.3 + Wi-Fi, boot organism | 1,428,000 B | 83% |
| rev1.3 + switch self-test | 1,337,744 B | 84% |

Zero compiler warnings. Host tests 4/4; ASan/UBSan clean.

## Pending / known limits

- Visual check: orientation, HOME hint/hotspot, and touch feel.
- Full 30-minute soak.
- Dummy draw is global display state; enable/disable is handled on mount/unmount.
- Only 0°/180° rotation is supported by the pre-rotation path.
- The HOME hint is drawn by the engine; the LVGL HOME button is not rendered in
  dummy-draw mode.
- PPA acceleration remains disabled.
