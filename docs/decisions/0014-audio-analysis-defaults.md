# ADR-0014 — Audio analysis defaults: half-octave bands and 20 ms FFT hop

**Status:** Accepted  
**Date:** 2026-11-06  
**Scope:** M6 implementation and later tuner/organism consumers

## Context

The architecture and ADR-0008 ask for “1024-point FFT, 16 bands” and also say
“1/3-octave”, which are not compatible over a useful range:

- 16 true 1/3-octave bands span only about 100 Hz–3.15 kHz.
- The guitar low E is ~82 Hz, so it would fall below that set.
- A 1024-point FFT at 16 kHz has 15.625 Hz bins. That is fine for a spectrum
  display and band energy, but far too coarse for tuning.

## Decision

- Use **16 half-octave bands**, centers:
  `44.2, 62.5, 88.4, 125, 176.8, 250, 353.6, 500, 707, 1000, 1414, 2000, 2828, 4000, 5657, 8000 Hz`.
  Edges are geometric midpoints; the first band starts near 31.25 Hz and the
  last is capped at Nyquist. Low E lands in the 88.4 Hz band.
- Use a **1024-point Hann FFT**, mono downmix for the spectrum, **20 ms hop**
  (320 samples, 68.75% overlap) for M6. A 32 ms / 50% overlap option can be
  added later if CPU headroom or spectrum cadence requires it.
- Keep **per-channel levels** (RMS, dBFS, peak, Leq) independent. The spectrum is
  mono-downmixed to stay within the 5 ms/frame analysis budget; per-channel FFT
  can be added later without changing the service API.
- Use a **1 s sliding Leq** per channel by default.
- The tuner uses **YIN/autocorrelation with parabolic interpolation**, not the
  1024-point FFT bins. If low-E accuracy misses the gate, add a dedicated
  higher-rate capture path rather than changing the shared 16 kHz format.

## Consequences

- The band set covers the guitar range and the full 16 kHz spectrum, but it is
  not a strict 1/3-octave analyzer. Update the wording in ADR-0008 and the
  architecture to say “16 half-octave bands”.
- A 20 ms hop gives a 50 Hz spectrum update and fits the 20 ms analysis task.
  The spectrum dB conversion is spread over four frames, so the full graph
  refreshes every 80 ms without a per-frame libm burst. A 32 ms option would
  cost less CPU but update at ~31 Hz.
- The tuner avoids the FFT resolution problem entirely; the spectrum graph is
  for visualization, not pitch measurement.
- If strict 1/3-octave analysis is ever required, add a second band table or a
  configurable band set; do not silently reinterpret the existing 16 bands.

See [`../architecture.md`](../architecture.md#audio-data-flow) and
[`../roadmap.md`](../roadmap.md).
