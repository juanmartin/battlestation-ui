# ADR-0008 — Audio baseline: 16 kHz stereo, 24 dB PGA, dBFS only

**Status:** Accepted  
**Date:** 2026-11-06

## Context

The ES7210 has four ADC channels, but only MIC1/MIC2 are microphones; MIC3/MIC4
are the speaker/AEC reference network. Both codecs share one duplex I2S
peripheral, so the audio service must own one format. The board has no acoustic
calibration data.

## Decision

Use:

| Parameter | Value |
| --- | --- |
| Sample rate | 16 kHz |
| Bit depth | 16-bit |
| Channels | 2 (MIC1/MIC2) |
| PGA gain | 24 dB initial |
| I2S DMA | 6 descriptors × ~240 frames |
| Ring buffer | 200 ms |
| Analysis frame | 1024 samples, Hann, 20 ms hop (68.75% overlap; 32 ms/50% option) |
| Metrics | RMS, dBFS, peak, 1 s Leq, 16 half-octave bands |

The 16-band table and FFT hop are defined in
[ADR-0014](0014-audio-analysis-defaults.md); the tuner uses YIN rather than the
FFT bins.

The UI label must say **dBFS**, not dB SPL. If a calibrated SPL value is needed
later, it requires a reference source and per-unit or per-model calibration.

## Consequences

- One format for capture and playback; no codec reconfiguration at runtime.
- 64 KB/s audio traffic; negligible CPU/memory.
- MIC3/MIC4 stay disabled unless AEC is deliberately implemented.
- 48 kHz can be added later for wider spectrum, but it is not the baseline.

See [`../architecture.md`](../architecture.md) and
[`../hardware.md`](../hardware.md).
