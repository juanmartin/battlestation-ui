# ADR-0013 — Audio output ownership and the tuner signal chain

**Status:** Accepted  
**Date:** 2026-11-06

## Context

M6 was originally scoped around microphone capture and analysis. The tuner/tone
lab adds a tone generator and a metronome, both of which need the ES8311 speaker
path. Capture and playback share one duplex I2S peripheral, and the codec driver
rejects conflicting formats. Letting each app open the codec would create
ownership and format conflicts.

The tuner also needs better frequency resolution than the dB/FFT meter: low E on
a guitar is ~82 Hz, while a 1024-point FFT at 16 kHz has ~15.6 Hz bins.

## Decision

`audio_service` owns **both directions** of the shared I2S bus:

- One open/close for ES7210 capture and ES8311 playback.
- Capture task feeds the analysis ring buffer.
- A bounded output mixer accepts PCM from the tone generator and metronome; apps
  submit buffers, they never call `esp_codec_dev_write`.
- The output mixer has a documented priority/ownership rule (tone lab owns the
  speaker while its app is active; notifications/metronome can mix at a lower
  level).

The tuner uses **YIN/autocorrelation with parabolic interpolation** on the
capture stream, not the 1024-point FFT bins. Validate low-E accuracy at 16 kHz;
if it is insufficient, add a dedicated higher-rate capture path rather than
silently changing the shared format.

The tone generator and metronome are pure C engines that synthesize into
buffers; they are testable on macOS and do not include codec or LVGL headers.

## Playback lanes

M6 implements three fixed lanes; one source per priority:

| Priority | Lane | Ring | Default trim | Use |
| ---: | --- | ---: | ---: | --- |
| 2 (highest) | `tone` | 80 ms | 0 dB | Guitar/tone generator |
| 1 | `metronome` | 80 ms | 0 dB | Click track |
| 0 | `notification` | 80 ms | 0 dB | UI/system sounds |

- Mono sources are duplicated to L/R by the service; apps always submit PCM.
- The mixer sums active lanes in `int32_t`, applies per-lane trim, saturates to
  `int16_t`, and lets ES8311 apply master volume.
- Empty/inactive lanes write silence. The service starts **muted**; M6's smoke
  test is capture + TX silence, with an optional test tone for hardware checks.
- Ducking when multiple lanes are active:

| Highest active lane | Tone | Metronome | Notification |
| --- | ---: | ---: | ---: |
| Tone | 0 dB | −9 dB | −15 dB |
| Metronome | — | 0 dB | −9 dB |
| Notification | — | — | 0 dB |

- “Active” means the app opened the lane and set it active, not merely “has
  data”. This keeps the ducking stable between metronome clicks.
- An underrun fills silence for that lane and increments a counter; it must not
  block the TX task or produce a click.

## Consequences

- M6 must define the playback API and mixer now, even if it starts with silence.
- Capture deadlines and playback deadlines are separate tasks; analysis must
  never run in the I2S read task.
- The tuner can be accurate without changing the 16 kHz analysis baseline.
- The tone lab and metronome share one output path and need a clear priority
  rule to avoid clipping or unexpected mixing.
- A future board port replaces the audio service backend, not the DSP engines.

See [`../architecture.md`](../architecture.md#audio-output-and-the-tone-lab) and
[`../roadmap.md`](../roadmap.md).
