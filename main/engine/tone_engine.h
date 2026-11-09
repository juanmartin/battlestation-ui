#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TONE_SINE_TABLE_SIZE 1024u

typedef enum {
    TONE_WAVE_SINE = 0,
    TONE_WAVE_SQUARE,
    TONE_WAVE_TRIANGLE,
    TONE_WAVE_SAW,
} tone_wave_t;

typedef enum {
    TONE_SWEEP_NONE = 0,
    TONE_SWEEP_LINEAR,
    TONE_SWEEP_LOG,
} tone_sweep_mode_t;

typedef struct {
    uint32_t sample_rate;   /* default 16000 */
    tone_wave_t waveform;   /* default sine */
    float frequency_hz;     /* default 440 */
    float amplitude;        /* 0..1, default 0.5 */
    tone_sweep_mode_t sweep_mode;
    float sweep_start_hz;
    float sweep_end_hz;
    float sweep_duration_s;
    float ramp_ms;          /* frequency/amplitude smoothing, default 5 ms */
} tone_engine_config_t;

/*
 * Caller-owned state. The sine wavetable lives here, so no allocation is done
 * by init or render. State transitions are sample-count based and therefore
 * deterministic across render block sizes.
 */
typedef struct {
    tone_engine_config_t config;

    uint32_t sample_rate;
    uint32_t ramp_samples;
    float phase_scale;

    uint32_t phase; /* Q32 cycle, wraps naturally */
    float frequency;
    float frequency_target;
    float frequency_coeff;

    float amplitude;
    float amplitude_target;
    float amplitude_step;
    uint32_t amplitude_ramp_left;

    tone_wave_t previous_waveform;
    uint32_t waveform_fade_remaining;
    uint32_t waveform_fade_total;

    bool sweep_active;
    uint32_t sweep_samples;
    uint32_t sweep_pos;
    float sweep_frequency;
    float sweep_step;
    float sweep_ratio;

    int16_t sine_table[TONE_SINE_TABLE_SIZE + 1u];
} tone_engine_t;

/*
 * Build the wavetable and reset phase/ramps. Returns false for an unusable
 * sample rate or a NULL engine/config. Invalid waveform/sweep values fall back
 * to safe defaults; frequencies and amplitude are clamped.
 *
 * If cfg->sweep_mode is not NONE the sweep starts immediately at
 * sweep_start_hz; otherwise the engine runs at frequency_hz.
 */
bool tone_engine_init(tone_engine_t *engine, const tone_engine_config_t *cfg);

/* Stop the tone, reset phase to zero and fade in from silence again. */
void tone_engine_reset(tone_engine_t *engine);

/* Ramp over ramp_ms; waveform switch crossfades over ramp_ms, keeping phase. */
bool tone_engine_set_waveform(tone_engine_t *engine, tone_wave_t waveform);
void tone_engine_set_frequency(tone_engine_t *engine, float frequency_hz);
void tone_engine_set_amplitude(tone_engine_t *engine, float amplitude);

/*
 * Start a sweep from start_hz to end_hz. The current frequency glides to the
 * sweep start over ramp_ms, then follows the sweep. Duration must be > 0 and
 * log sweeps require positive endpoints.
 */
bool tone_engine_start_sweep(tone_engine_t *engine, tone_sweep_mode_t mode,
                             float start_hz, float end_hz, float duration_s);

/* Hold the current sweep frequency (or frequency_hz if no sweep is active). */
void tone_engine_stop_sweep(tone_engine_t *engine);

/* Render interleaved stereo or mono int16. Phase is continuous across calls. */
void tone_engine_render(tone_engine_t *engine, int16_t *stereo, size_t frames);
void tone_engine_render_mono(tone_engine_t *engine, int16_t *mono,
                             size_t frames);
