#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_seqlock.h"

#define TUNER_ENGINE_DEFAULT_SAMPLE_RATE 16000u
#define TUNER_ENGINE_DEFAULT_WINDOW      2048u
#define TUNER_ENGINE_DEFAULT_MIN_HZ      40.0f
#define TUNER_ENGINE_DEFAULT_MAX_HZ      1200.0f
#define TUNER_ENGINE_DEFAULT_A4_HZ       440.0f
#define TUNER_ENGINE_DEFAULT_THRESHOLD   0.15f
#define TUNER_ENGINE_DEFAULT_MIN_RMS     0.0005f
#define TUNER_ENGINE_DEFAULT_CONFIDENCE  0.5f
#define TUNER_ENGINE_NOTE_NAME_MAX       4u
#define TUNER_ENGINE_MAX_WINDOW          65536u

typedef void *(*tuner_alloc_fn)(size_t size);
typedef void (*tuner_free_fn)(void *ptr);

/*
 * Pitch detector configuration. Zero values select defaults. The engine is
 * mono; feed one channel (or a downmix) from the capture stream.
 *
 * window_size is the YIN integration window in samples (2048 = 128 ms at
 * 16 kHz). hop_size controls how often a new result is published; zero means
 * window_size / 2. 48 kHz input should normally use a 4096-sample window so
 * low E spans enough periods.
 */
typedef struct {
    uint32_t sample_rate;
    uint32_t window_size;
    uint32_t hop_size;
    float min_frequency_hz;
    float max_frequency_hz;
    float a4_hz;
    float threshold;      /* YIN absolute threshold, typically 0.10..0.20 */
    float min_rms;        /* normalized RMS (1.0 = full scale) below which
                             silence is reported */
    float min_confidence; /* 0..1 acceptance gate for a valid result */
    tuner_alloc_fn alloc; /* required */
    tuner_free_fn free;   /* required */
} tuner_engine_config_t;

typedef struct {
    float frequency_hz;
    float confidence; /* 0..1 */
    float cents;      /* deviation from the nearest equal-tempered note */
    int16_t midi_note;
    int8_t octave;
    char note_name[TUNER_ENGINE_NOTE_NAME_MAX]; /* "A", "C#", ... */
    bool valid;
    uint32_t updates; /* analysis updates since reset */
} tuner_result_t;

typedef struct tuner_engine tuner_engine_t;

/*
 * All state is allocated with cfg->alloc and released with cfg->free.
 * Returns NULL for an invalid configuration or allocation failure.
 */
tuner_engine_t *tuner_engine_create(const tuner_engine_config_t *cfg);
void tuner_engine_destroy(tuner_engine_t *engine);

/* Drop the buffered window and published result. */
void tuner_engine_reset(tuner_engine_t *engine);

/*
 * Feed any number of mono samples. The engine buffers until a full window is
 * available, then publishes a result every hop_size samples. Returns true when
 * this call produced at least one new analysis.
 */
bool tuner_engine_process_block(tuner_engine_t *engine,
                                const int16_t *samples,
                                size_t frames);

/*
 * Copy the latest result. Safe to call from a different task than the writer
 * (seqlock snapshot). Returns false only if no consistent copy was obtained.
 */
bool tuner_engine_get_result(const tuner_engine_t *engine,
                             tuner_result_t *out);

/* Runtime reference change (M10 settings). Invalid values are ignored. */
void tuner_engine_set_a4_reference(tuner_engine_t *engine, float a4_hz);

/* Runtime YIN threshold change; invalid values are ignored. */
void tuner_engine_set_threshold(tuner_engine_t *engine, float threshold);
