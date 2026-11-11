#include "tone_engine.h"

#include <math.h>
#include <string.h>

#define TONE_TWO_PI 6.283185307179586476925286766559
#define TONE_MIN_SAMPLE_RATE 4000u
#define TONE_MAX_SAMPLE_RATE 192000u
#define TONE_DEFAULT_SAMPLE_RATE 16000u
#define TONE_DEFAULT_FREQUENCY 440.0f
#define TONE_DEFAULT_AMPLITUDE 0.5f
#define TONE_DEFAULT_RAMP_MS 5.0f
#define TONE_TABLE_BITS 10u
#define TONE_TABLE_INDEX_SHIFT (32u - TONE_TABLE_BITS)
#define TONE_TABLE_FRAC_BITS 12u
#define TONE_TABLE_FRAC_MASK ((1u << TONE_TABLE_FRAC_BITS) - 1u)
#define TONE_TABLE_FRAC_SHIFT (32u - TONE_TABLE_BITS - TONE_TABLE_FRAC_BITS)

_Static_assert(TONE_SINE_TABLE_SIZE == (1u << TONE_TABLE_BITS),
               "sine table size must match the phase index bits");

static bool finite_value(float value)
{
    return isfinite(value);
}

static float clamp_unit(float value)
{
    if (!finite_value(value) || value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float clamp_frequency(const tone_engine_t *engine, float hz)
{
    float max_hz = (float)engine->sample_rate * 0.5f - 1.0f;
    if (max_hz < 1.0f) {
        max_hz = 1.0f;
    }
    if (!finite_value(hz) || hz < 1.0f) {
        return 1.0f;
    }
    if (hz > max_hz) {
        return max_hz;
    }
    return hz;
}

static bool valid_waveform(tone_wave_t waveform)
{
    return waveform == TONE_WAVE_SINE || waveform == TONE_WAVE_SQUARE ||
           waveform == TONE_WAVE_TRIANGLE || waveform == TONE_WAVE_SAW;
}

static void build_sine_table(tone_engine_t *engine)
{
    for (uint32_t i = 0; i < TONE_SINE_TABLE_SIZE; i++) {
        double phase = TONE_TWO_PI * (double)i / (double)TONE_SINE_TABLE_SIZE;
        engine->sine_table[i] = (int16_t)lrint(sin(phase) * 32767.0);
    }
    engine->sine_table[TONE_SINE_TABLE_SIZE] = engine->sine_table[0];
}

static void update_sweep_wiring(tone_engine_t *engine)
{
    engine->sweep_samples =
        (uint32_t)lrintf(engine->config.sweep_duration_s *
                         (float)engine->sample_rate);
    if (engine->sweep_samples == 0) {
        engine->sweep_samples = 1;
    }
    engine->sweep_pos = 0;
    engine->sweep_frequency = engine->config.sweep_start_hz;
    engine->sweep_active = engine->config.sweep_mode != TONE_SWEEP_NONE;
    engine->sweep_step = 0.0f;
    engine->sweep_ratio = 1.0f;

    if (!engine->sweep_active) {
        return;
    }

    if (engine->config.sweep_mode == TONE_SWEEP_LINEAR) {
        engine->sweep_step =
            (engine->config.sweep_end_hz - engine->config.sweep_start_hz) /
            (float)engine->sweep_samples;
    } else {
        float ratio = engine->config.sweep_end_hz /
                      engine->config.sweep_start_hz;
        engine->sweep_ratio =
            powf(ratio, 1.0f / (float)engine->sweep_samples);
    }
}

bool tone_engine_init(tone_engine_t *engine, const tone_engine_config_t *cfg)
{
    if (engine == NULL || cfg == NULL) {
        return false;
    }

    memset(engine, 0, sizeof(*engine));

    uint32_t sample_rate = cfg->sample_rate;
    if (sample_rate == 0) {
        sample_rate = TONE_DEFAULT_SAMPLE_RATE;
    }
    if (sample_rate < TONE_MIN_SAMPLE_RATE ||
        sample_rate > TONE_MAX_SAMPLE_RATE) {
        return false;
    }
    engine->sample_rate = sample_rate;

    tone_wave_t waveform =
        valid_waveform(cfg->waveform) ? cfg->waveform : TONE_WAVE_SINE;
    tone_sweep_mode_t sweep_mode =
        (cfg->sweep_mode == TONE_SWEEP_LINEAR ||
         cfg->sweep_mode == TONE_SWEEP_LOG)
            ? cfg->sweep_mode
            : TONE_SWEEP_NONE;

    float frequency = cfg->frequency_hz;
    if (!finite_value(frequency) || frequency <= 0.0f) {
        frequency = TONE_DEFAULT_FREQUENCY;
    }

    float amplitude = cfg->amplitude;
    if (!finite_value(amplitude)) {
        amplitude = TONE_DEFAULT_AMPLITUDE;
    }
    amplitude = clamp_unit(amplitude);

    float ramp_ms = cfg->ramp_ms;
    if (!finite_value(ramp_ms) || ramp_ms <= 0.0f) {
        ramp_ms = TONE_DEFAULT_RAMP_MS;
    }
    if (ramp_ms > 1000.0f) {
        ramp_ms = 1000.0f;
    }

    engine->config.sample_rate = sample_rate;
    engine->config.waveform = waveform;
    engine->config.amplitude = amplitude;
    engine->config.sweep_mode = sweep_mode;
    engine->config.ramp_ms = ramp_ms;

    engine->config.frequency_hz = clamp_frequency(engine, frequency);

    float sweep_start = cfg->sweep_start_hz;
    float sweep_end = cfg->sweep_end_hz;
    if (!finite_value(sweep_start) || sweep_start <= 0.0f) {
        sweep_start = engine->config.frequency_hz;
    }
    if (!finite_value(sweep_end) || sweep_end <= 0.0f) {
        sweep_end = engine->config.frequency_hz;
    }
    engine->config.sweep_start_hz = clamp_frequency(engine, sweep_start);
    engine->config.sweep_end_hz = clamp_frequency(engine, sweep_end);

    float duration = cfg->sweep_duration_s;
    if (!finite_value(duration) || duration <= 0.0f) {
        duration = 0.0f;
    } else if (duration > 3600.0f) {
        duration = 3600.0f;
    }
    engine->config.sweep_duration_s = duration;
    if (sweep_mode != TONE_SWEEP_NONE && duration <= 0.0f) {
        engine->config.sweep_mode = TONE_SWEEP_NONE;
    }

    engine->ramp_samples =
        (uint32_t)lrintf(ramp_ms * 0.001f * (float)sample_rate);
    if (engine->ramp_samples == 0) {
        engine->ramp_samples = 1;
    }
    engine->frequency_coeff =
        1.0f - expf(-1.0f / (float)engine->ramp_samples);
    engine->phase_scale = 4294967296.0f / (float)sample_rate;

    build_sine_table(engine);
    tone_engine_reset(engine);
    return true;
}

void tone_engine_reset(tone_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }

    engine->phase = 0;
    engine->frequency =
        (engine->config.sweep_mode != TONE_SWEEP_NONE)
            ? engine->config.sweep_start_hz
            : engine->config.frequency_hz;
    engine->frequency_target = engine->frequency;

    engine->previous_waveform = engine->config.waveform;
    engine->waveform_fade_remaining = 0;
    engine->waveform_fade_total = 0;

    engine->amplitude = 0.0f;
    engine->amplitude_target = clamp_unit(engine->config.amplitude);
    engine->amplitude_ramp_left = 0;
    engine->amplitude_step = 0.0f;
    if (engine->amplitude_target > 0.0f) {
        engine->amplitude_step =
            engine->amplitude_target / (float)engine->ramp_samples;
        engine->amplitude_ramp_left = engine->ramp_samples;
    }

    update_sweep_wiring(engine);
}

bool tone_engine_set_waveform(tone_engine_t *engine, tone_wave_t waveform)
{
    if (engine == NULL || !valid_waveform(waveform)) {
        return false;
    }
    if (waveform == engine->config.waveform) {
        return true;
    }
    engine->previous_waveform = engine->config.waveform;
    engine->config.waveform = waveform;
    engine->waveform_fade_total = engine->ramp_samples;
    engine->waveform_fade_remaining = engine->ramp_samples;
    return true;
}

void tone_engine_set_frequency(tone_engine_t *engine, float frequency_hz)
{
    if (engine == NULL || !finite_value(frequency_hz)) {
        return;
    }

    float frequency = clamp_frequency(engine, frequency_hz);
    engine->config.frequency_hz = frequency;
    engine->config.sweep_mode = TONE_SWEEP_NONE;
    engine->sweep_active = false;
    engine->frequency_target = frequency;
}

void tone_engine_set_amplitude(tone_engine_t *engine, float amplitude)
{
    if (engine == NULL || !finite_value(amplitude)) {
        return;
    }

    amplitude = clamp_unit(amplitude);
    engine->config.amplitude = amplitude;
    engine->amplitude_target = amplitude;
    if (engine->ramp_samples == 0) {
        engine->amplitude = amplitude;
        engine->amplitude_ramp_left = 0;
        engine->amplitude_step = 0.0f;
    } else {
        engine->amplitude_step =
            (amplitude - engine->amplitude) / (float)engine->ramp_samples;
        engine->amplitude_ramp_left = engine->ramp_samples;
    }
}

bool tone_engine_start_sweep(tone_engine_t *engine, tone_sweep_mode_t mode,
                             float start_hz, float end_hz, float duration_s)
{
    if (engine == NULL || !finite_value(start_hz) ||
        !finite_value(end_hz) || !finite_value(duration_s)) {
        return false;
    }

    if (mode == TONE_SWEEP_NONE) {
        tone_engine_stop_sweep(engine);
        return true;
    }
    if (mode != TONE_SWEEP_LINEAR && mode != TONE_SWEEP_LOG) {
        return false;
    }
    if (start_hz <= 0.0f || end_hz <= 0.0f || duration_s <= 0.0f) {
        return false;
    }
    if (duration_s > 3600.0f) {
        duration_s = 3600.0f;
    }

    engine->config.sweep_mode = mode;
    engine->config.sweep_start_hz = clamp_frequency(engine, start_hz);
    engine->config.sweep_end_hz = clamp_frequency(engine, end_hz);
    engine->config.sweep_duration_s = duration_s;
    update_sweep_wiring(engine);
    engine->frequency_target = engine->sweep_frequency;
    return true;
}

void tone_engine_stop_sweep(tone_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }
    engine->config.sweep_mode = TONE_SWEEP_NONE;
    engine->sweep_active = false;
    engine->config.frequency_hz = engine->frequency;
    engine->frequency_target = engine->frequency;
}

static int32_t sine_sample(const tone_engine_t *engine, uint32_t phase)
{
    uint32_t index = phase >> TONE_TABLE_INDEX_SHIFT;
    uint32_t frac = (phase >> TONE_TABLE_FRAC_SHIFT) & TONE_TABLE_FRAC_MASK;
    int32_t a = engine->sine_table[index];
    int32_t b = engine->sine_table[index + 1u];
    return a + (int32_t)(((b - a) * (int32_t)frac) >>
                        TONE_TABLE_FRAC_BITS);
}

static int32_t waveform_sample(const tone_engine_t *engine,
                               tone_wave_t waveform, uint32_t phase)
{
    switch (waveform) {
    case TONE_WAVE_SQUARE:
        return (phase < 0x80000000u) ? 32767 : -32767;
    case TONE_WAVE_TRIANGLE: {
        uint32_t p = phase >> 16;
        if (p < 16384u) {
            return (int32_t)p * 2;
        }
        if (p < 32768u) {
            return 32768 - (int32_t)(p - 16384u) * 2;
        }
        if (p < 49152u) {
            return -(int32_t)(p - 32768u) * 2;
        }
        return -32768 + (int32_t)(p - 49152u) * 2;
    }
    case TONE_WAVE_SAW:
        return (int32_t)(phase >> 16) - 32768;
    case TONE_WAVE_SINE:
    default:
        return sine_sample(engine, phase);
    }
}

static int16_t tone_next_sample(tone_engine_t *engine)
{
    if (engine->sweep_active) {
        if (engine->sweep_pos + 1u >= engine->sweep_samples) {
            engine->sweep_frequency = engine->config.sweep_end_hz;
            engine->sweep_active = false;
        } else {
            engine->frequency_target = engine->sweep_frequency;
            if (engine->config.sweep_mode == TONE_SWEEP_LINEAR) {
                engine->sweep_frequency += engine->sweep_step;
            } else {
                engine->sweep_frequency *= engine->sweep_ratio;
            }
        }
        engine->sweep_pos++;
        if (!engine->sweep_active) {
            engine->frequency_target = engine->sweep_frequency;
        }
    }

    engine->frequency +=
        (engine->frequency_target - engine->frequency) *
        engine->frequency_coeff;

    if (engine->amplitude_ramp_left > 0u) {
        engine->amplitude += engine->amplitude_step;
        engine->amplitude_ramp_left--;
        if (engine->amplitude_ramp_left == 0u) {
            engine->amplitude = engine->amplitude_target;
        }
        engine->amplitude = clamp_unit(engine->amplitude);
    }

    uint32_t increment =
        (uint32_t)(engine->frequency * engine->phase_scale);
    engine->phase += increment;

    int32_t wave =
        waveform_sample(engine, engine->config.waveform, engine->phase);
    if (engine->waveform_fade_remaining > 0u) {
        int32_t previous = waveform_sample(engine, engine->previous_waveform,
                                           engine->phase);
        uint32_t elapsed =
            engine->waveform_fade_total - engine->waveform_fade_remaining;
        float mix = (float)(elapsed + 1u) / (float)engine->waveform_fade_total;
        wave = (int32_t)((float)previous * (1.0f - mix) +
                         (float)wave * mix);
        engine->waveform_fade_remaining--;
    }

    float sample = (float)wave * engine->amplitude;
    int32_t value =
        (int32_t)(sample >= 0.0f ? sample + 0.5f : sample - 0.5f);
    if (value > 32767) {
        value = 32767;
    } else if (value < -32768) {
        value = -32768;
    }
    return (int16_t)value;
}

void tone_engine_render(tone_engine_t *engine, int16_t *stereo, size_t frames)
{
    if (engine == NULL || stereo == NULL) {
        return;
    }

    for (size_t i = 0; i < frames; i++) {
        int16_t sample = tone_next_sample(engine);
        stereo[i * 2u + 0u] = sample;
        stereo[i * 2u + 1u] = sample;
    }
}

void tone_engine_render_mono(tone_engine_t *engine, int16_t *mono,
                             size_t frames)
{
    if (engine == NULL || mono == NULL) {
        return;
    }

    for (size_t i = 0; i < frames; i++) {
        mono[i] = tone_next_sample(engine);
    }
}
