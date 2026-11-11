#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define METRONOME_DEFAULT_SAMPLE_RATE 16000u
#define METRONOME_DEFAULT_BPM         120.0f
#define METRONOME_DEFAULT_CLICK_MS    30.0f
#define METRONOME_DEFAULT_ATTACK_MS   1.0f
#define METRONOME_DEFAULT_ACCENT_HZ   1760.0f
#define METRONOME_DEFAULT_BEAT_HZ     880.0f
#define METRONOME_DEFAULT_ACCENT_GAIN 0.9f
#define METRONOME_DEFAULT_BEAT_GAIN   0.6f

typedef enum {
    METRONOME_METER_2_4 = 2,
    METRONOME_METER_3_4 = 3,
    METRONOME_METER_4_4 = 4,
    METRONOME_METER_6_8 = 6,
} metronome_meter_t;

typedef enum {
    METRONOME_CLICK_SINE = 0,
    METRONOME_CLICK_SQUARE,
} metronome_click_wave_t;

typedef void (*metronome_beat_cb_t)(void *user, uint64_t sample_index,
                                    uint8_t beat_in_bar, bool accent);

typedef struct {
    uint32_t sample_rate; /* default 16000 */
    float bpm;            /* 30..240, default 120 */
    metronome_meter_t meter; /* 2/4, 3/4, 4/4, 6/8; default 4/4 */
    uint8_t accent_beat;     /* 1-based, default 1 */
    metronome_click_wave_t click_wave;
    float accent_frequency_hz;
    float beat_frequency_hz;
    float accent_gain; /* 0..1 */
    float beat_gain;   /* 0..1 */
    float click_ms;    /* click envelope length, default 30 ms */
    float attack_ms;   /* envelope attack, default 1 ms */
} metronome_config_t;

/*
 * Caller-owned state, no allocation. BPM is the number of pulses per minute;
 * in 6/8 that means six pulses per bar, with the bar grouping/accent deciding
 * which pulse is accented. click_ms is clamped to 200 ms so a click always
 * finishes before the next beat at the maximum 240 BPM.
 */
typedef struct {
    uint32_t sample_rate;
    uint32_t beats_per_bar;
    uint8_t accent_beat;
    float bpm;
    uint64_t period_q32;  /* samples per beat in Q32.32 */
    uint64_t beat_acc_q32; /* time until the next beat */
    uint64_t total_samples;
    uint32_t beat_count;
    uint32_t bar_count;
    uint8_t beat_in_bar; /* 0 before the first beat, else 1..beats_per_bar */

    metronome_click_wave_t click_wave;
    float accent_frequency_hz;
    float beat_frequency_hz;
    float accent_gain;
    float beat_gain;
    uint32_t click_length;
    uint32_t attack_length;
    float click_phase;
    float click_phase_inc;
    uint32_t click_pos;
    uint32_t click_remaining;
    float click_gain;

    metronome_beat_cb_t beat_cb;
    void *beat_cb_user;
} metronome_t;

bool metronome_init(metronome_t *engine, const metronome_config_t *cfg);

/* Restart at sample 0; the first rendered sample triggers beat 1. */
void metronome_reset(metronome_t *engine);

/*
 * The current interval is preserved; the new tempo applies from the next
 * beat. The click waveform and envelope are never interrupted, so changing
 * BPM cannot introduce a discontinuity.
 */
void metronome_set_bpm(metronome_t *engine, float bpm);
void metronome_set_meter(metronome_t *engine, metronome_meter_t meter);
void metronome_set_accent_beat(metronome_t *engine, uint8_t accent_beat);
bool metronome_set_click_wave(metronome_t *engine,
                              metronome_click_wave_t wave);

/* Optional beat callback, invoked from render at the exact trigger sample. */
void metronome_set_beat_callback(metronome_t *engine,
                                 metronome_beat_cb_t callback, void *user);

/* Render interleaved stereo or mono int16 click samples. */
void metronome_render(metronome_t *engine, int16_t *stereo, size_t frames);
void metronome_render_mono(metronome_t *engine, int16_t *mono, size_t frames);

/* Visual beat counters. beat_in_bar is 0 until the first beat. */
uint32_t metronome_beat_count(const metronome_t *engine);
uint32_t metronome_bar_count(const metronome_t *engine);
uint8_t metronome_beat_in_bar(const metronome_t *engine);
