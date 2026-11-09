#include "metronome.h"

#include <math.h>
#include <string.h>

#define METRONOME_TWO_PI 6.283185307179586476925286766559
#define METRONOME_ONE_Q32 4294967296.0
#define METRONOME_MIN_SAMPLE_RATE 4000u
#define METRONOME_MAX_SAMPLE_RATE 192000u

static uint64_t period_from_bpm(uint32_t sample_rate, float bpm)
{
    double samples = (double)sample_rate * 60.0 / (double)bpm;
    double q32 = samples * METRONOME_ONE_Q32;
    if (q32 < METRONOME_ONE_Q32) {
        q32 = METRONOME_ONE_Q32;
    }
    return (uint64_t)(q32 + 0.5);
}

static float clamp_unit(float value, float fallback)
{
    if (!isfinite(value)) {
        return fallback;
    }
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float clamp_click_frequency(uint32_t sample_rate, float hz,
                                   float fallback)
{
    float max_hz = (float)sample_rate * 0.45f;
    if (!isfinite(hz) || hz <= 0.0f) {
        hz = fallback;
    }
    if (hz < 20.0f) {
        hz = 20.0f;
    }
    if (hz > max_hz) {
        hz = max_hz;
    }
    return hz;
}

static bool valid_meter(metronome_meter_t meter)
{
    return meter == METRONOME_METER_2_4 || meter == METRONOME_METER_3_4 ||
           meter == METRONOME_METER_4_4 || meter == METRONOME_METER_6_8;
}

static bool valid_click_wave(metronome_click_wave_t wave)
{
    return wave == METRONOME_CLICK_SINE || wave == METRONOME_CLICK_SQUARE;
}

bool metronome_init(metronome_t *engine, const metronome_config_t *cfg)
{
    if (engine == NULL || cfg == NULL) {
        return false;
    }

    memset(engine, 0, sizeof(*engine));

    uint32_t sample_rate = cfg->sample_rate;
    if (sample_rate == 0) {
        sample_rate = METRONOME_DEFAULT_SAMPLE_RATE;
    }
    if (sample_rate < METRONOME_MIN_SAMPLE_RATE ||
        sample_rate > METRONOME_MAX_SAMPLE_RATE) {
        return false;
    }
    engine->sample_rate = sample_rate;

    float bpm = cfg->bpm;
    if (!isfinite(bpm) || bpm <= 0.0f) {
        bpm = METRONOME_DEFAULT_BPM;
    }
    if (bpm < 30.0f) {
        bpm = 30.0f;
    } else if (bpm > 240.0f) {
        bpm = 240.0f;
    }
    engine->bpm = bpm;

    engine->beats_per_bar =
        valid_meter(cfg->meter) ? (uint32_t)cfg->meter : 4u;

    uint8_t accent_beat = cfg->accent_beat;
    if (accent_beat == 0) {
        accent_beat = 1;
    }
    if (accent_beat > engine->beats_per_bar) {
        accent_beat = (uint8_t)engine->beats_per_bar;
    }
    engine->accent_beat = accent_beat;

    engine->click_wave = valid_click_wave(cfg->click_wave)
                             ? cfg->click_wave
                             : METRONOME_CLICK_SINE;

    engine->accent_frequency_hz =
        clamp_click_frequency(sample_rate, cfg->accent_frequency_hz,
                              METRONOME_DEFAULT_ACCENT_HZ);
    engine->beat_frequency_hz =
        clamp_click_frequency(sample_rate, cfg->beat_frequency_hz,
                              METRONOME_DEFAULT_BEAT_HZ);
    engine->accent_gain = clamp_unit(cfg->accent_gain,
                                     METRONOME_DEFAULT_ACCENT_GAIN);
    engine->beat_gain =
        clamp_unit(cfg->beat_gain, METRONOME_DEFAULT_BEAT_GAIN);

    float click_ms = cfg->click_ms;
    if (!isfinite(click_ms) || click_ms <= 0.0f) {
        click_ms = METRONOME_DEFAULT_CLICK_MS;
    }
    if (click_ms < 1.0f) {
        click_ms = 1.0f;
    } else if (click_ms > 200.0f) {
        click_ms = 200.0f;
    }

    float attack_ms = cfg->attack_ms;
    if (!isfinite(attack_ms) || attack_ms < 0.0f) {
        attack_ms = METRONOME_DEFAULT_ATTACK_MS;
    }
    if (attack_ms > click_ms) {
        attack_ms = click_ms;
    }

    engine->click_length =
        (uint32_t)lrintf(click_ms * 0.001f * (float)sample_rate);
    if (engine->click_length < 2u) {
        engine->click_length = 2u;
    }
    engine->attack_length =
        (uint32_t)lrintf(attack_ms * 0.001f * (float)sample_rate);
    if (engine->attack_length < 1u) {
        engine->attack_length = 1u;
    }
    if (engine->attack_length >= engine->click_length) {
        engine->attack_length = engine->click_length - 1u;
    }

    engine->period_q32 = period_from_bpm(sample_rate, bpm);
    metronome_reset(engine);
    return true;
}

void metronome_reset(metronome_t *engine)
{
    if (engine == NULL) {
        return;
    }

    engine->period_q32 = period_from_bpm(engine->sample_rate, engine->bpm);
    engine->beat_acc_q32 = 0;
    engine->total_samples = 0;
    engine->beat_count = 0;
    engine->bar_count = 0;
    engine->beat_in_bar = 0;
    engine->click_phase = 0.0f;
    engine->click_phase_inc = 0.0f;
    engine->click_pos = 0;
    engine->click_remaining = 0;
    engine->click_gain = 0.0f;
}

static void trigger_beat(metronome_t *engine, uint64_t sample_index)
{
    uint8_t beat;
    if (engine->beat_in_bar == 0) {
        beat = 1;
    } else if (engine->beat_in_bar >= engine->beats_per_bar) {
        beat = 1;
        engine->bar_count++;
    } else {
        beat = (uint8_t)(engine->beat_in_bar + 1u);
    }
    engine->beat_in_bar = beat;
    engine->beat_count++;

    bool accent = beat == engine->accent_beat;
    float frequency =
        accent ? engine->accent_frequency_hz : engine->beat_frequency_hz;
    engine->click_phase = 0.0f;
    engine->click_phase_inc =
        (float)METRONOME_TWO_PI * frequency / (float)engine->sample_rate;
    engine->click_pos = 0;
    engine->click_remaining = engine->click_length;
    engine->click_gain = accent ? engine->accent_gain : engine->beat_gain;

    if (engine->beat_cb != NULL) {
        engine->beat_cb(engine->beat_cb_user, sample_index, beat, accent);
    }
}

static int16_t click_sample(metronome_t *engine)
{
    int32_t value = 0;

    if (engine->click_remaining > 0u) {
        uint32_t pos = engine->click_pos;
        uint32_t length = engine->click_length;
        float t = (length > 1u) ? (float)pos / (float)(length - 1u) : 1.0f;
        float attack = (float)pos / (float)engine->attack_length;
        if (attack > 1.0f) {
            attack = 1.0f;
        }
        float decay = 1.0f - t;
        decay *= decay;
        float envelope = attack * decay;

        float wave;
        if (engine->click_wave == METRONOME_CLICK_SINE) {
            wave = sinf(engine->click_phase);
        } else {
            wave = (engine->click_phase < 3.14159265358979323846f) ? 1.0f
                                                                   : -1.0f;
        }

        float sample = wave * envelope * engine->click_gain * 32767.0f;
        value = (int32_t)(sample >= 0.0f ? sample + 0.5f : sample - 0.5f);

        engine->click_phase += engine->click_phase_inc;
        if (engine->click_phase >= (float)METRONOME_TWO_PI) {
            engine->click_phase -= (float)METRONOME_TWO_PI;
        }
        engine->click_pos++;
        engine->click_remaining--;
    }

    if (value > 32767) {
        value = 32767;
    } else if (value < -32768) {
        value = -32768;
    }
    return (int16_t)value;
}

static int16_t metronome_next_frame(metronome_t *engine)
{
    const uint64_t one_q32 = (uint64_t)1u << 32u;

    if (engine->beat_acc_q32 < one_q32) {
        trigger_beat(engine, engine->total_samples);
        engine->beat_acc_q32 += engine->period_q32;
    }

    int16_t sample = click_sample(engine);

    if (engine->beat_acc_q32 >= one_q32) {
        engine->beat_acc_q32 -= one_q32;
    } else {
        engine->beat_acc_q32 = 0;
    }
    engine->total_samples++;
    return sample;
}

void metronome_render(metronome_t *engine, int16_t *stereo, size_t frames)
{
    if (engine == NULL || stereo == NULL || engine->sample_rate == 0u) {
        return;
    }

    for (size_t i = 0; i < frames; i++) {
        int16_t sample = metronome_next_frame(engine);
        stereo[i * 2u + 0u] = sample;
        stereo[i * 2u + 1u] = sample;
    }
}

void metronome_render_mono(metronome_t *engine, int16_t *mono, size_t frames)
{
    if (engine == NULL || mono == NULL || engine->sample_rate == 0u) {
        return;
    }

    for (size_t i = 0; i < frames; i++) {
        mono[i] = metronome_next_frame(engine);
    }
}

void metronome_set_bpm(metronome_t *engine, float bpm)
{
    if (engine == NULL || !isfinite(bpm) || bpm <= 0.0f) {
        return;
    }
    if (bpm < 30.0f) {
        bpm = 30.0f;
    } else if (bpm > 240.0f) {
        bpm = 240.0f;
    }
    engine->bpm = bpm;
    engine->period_q32 = period_from_bpm(engine->sample_rate, bpm);
}

void metronome_set_meter(metronome_t *engine, metronome_meter_t meter)
{
    if (engine == NULL || !valid_meter(meter)) {
        return;
    }
    engine->beats_per_bar = (uint32_t)meter;
    if (engine->accent_beat > engine->beats_per_bar) {
        engine->accent_beat = (uint8_t)engine->beats_per_bar;
    }
}

void metronome_set_accent_beat(metronome_t *engine, uint8_t accent_beat)
{
    if (engine == NULL) {
        return;
    }
    if (accent_beat == 0) {
        accent_beat = 1;
    }
    if (accent_beat > engine->beats_per_bar) {
        accent_beat = (uint8_t)engine->beats_per_bar;
    }
    engine->accent_beat = accent_beat;
}

bool metronome_set_click_wave(metronome_t *engine,
                              metronome_click_wave_t wave)
{
    if (engine == NULL || !valid_click_wave(wave)) {
        return false;
    }
    engine->click_wave = wave;
    return true;
}

void metronome_set_beat_callback(metronome_t *engine,
                                 metronome_beat_cb_t callback, void *user)
{
    if (engine == NULL) {
        return;
    }
    engine->beat_cb = callback;
    engine->beat_cb_user = user;
}

uint32_t metronome_beat_count(const metronome_t *engine)
{
    return (engine != NULL) ? engine->beat_count : 0u;
}

uint32_t metronome_bar_count(const metronome_t *engine)
{
    return (engine != NULL) ? engine->bar_count : 0u;
}

uint8_t metronome_beat_in_bar(const metronome_t *engine)
{
    return (engine != NULL) ? engine->beat_in_bar : 0u;
}
