#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_fft.h"

#define AUDIO_BAND_COUNT 16u
#define AUDIO_LEQ_BLOCKS 100u
#define AUDIO_FEATURE_FLOOR_DB (-120.0f)

/* Rolling state for the 10 ms level and Leq processor. */
typedef struct {
    bool have_smooth;
    float smooth_l;
    float smooth_r;
    float peak_hold;
    uint32_t frames;
    float leq_power_l[AUDIO_LEQ_BLOCKS];
    float leq_power_r[AUDIO_LEQ_BLOCKS];
    uint32_t leq_index;
    uint32_t leq_count;
    double leq_sum_l;
    double leq_sum_r;
} audio_levels_ctx_t;

/* Published levels snapshot; field names match the legacy probe snapshot. */
typedef struct {
    float dbfs_l;
    float dbfs_r;
    float dbfs_avg;
    float peak_dbfs;
    float leq_l;
    float leq_r;
    uint32_t frames;
    bool running;
    bool error;
} audio_levels_t;

typedef struct {
    audio_levels_t levels;
    uint32_t seq;
    uint64_t timestamp_us;
    uint32_t fft_size;   /* samples per transform: AUDIO_FFT_SIZE */
    uint32_t fft_count;  /* transforms computed since start */
    uint16_t fft_bins;   /* one-sided bins: AUDIO_FFT_BINS */
    float bin_hz;
    float spectrum_db[AUDIO_FFT_BINS];
    float band_db[AUDIO_BAND_COUNT];
    uint32_t analysis_time_us;
} audio_features_t;

typedef struct {
    audio_levels_ctx_t levels;
    audio_fft_t fft;
    float history[AUDIO_FFT_SIZE];
    float fft_input[AUDIO_FFT_SIZE];
    size_t history_count;
    size_t history_pos;
    uint32_t fft_count;
    float band_edges[AUDIO_BAND_COUNT + 1u];
    uint16_t band_bin_start[AUDIO_BAND_COUNT];
    uint16_t band_bin_end[AUDIO_BAND_COUNT];
    float spectrum_power[AUDIO_FFT_BINS];
    float spectrum_db[AUDIO_FFT_BINS];
} audio_features_ctx_t;

/* Nominal half-octave band centers (ADR-0014). */
extern const float audio_features_band_centers_hz[AUDIO_BAND_COUNT];

void audio_features_levels_init(audio_levels_ctx_t *ctx);

/*
 * Process one block of AUDIO_FRAME_SAMPLES frames of interleaved stereo
 * samples. Pure C: no logs, no ESP-IDF/FreeRTOS/LVGL dependencies.
 */
void audio_features_process_levels(audio_levels_ctx_t *ctx,
                                   const int16_t *frame,
                                   size_t samples_per_channel,
                                   audio_levels_t *out);

void audio_features_init(audio_features_ctx_t *ctx);

/*
 * Process a PCM block: updates levels in AUDIO_FRAME_SAMPLES chunks, appends
 * the mono downmix to the FFT history, and runs one FFT when at least
 * AUDIO_FFT_SIZE samples are available. The service fills seq, timestamp_us
 * and analysis_time_us when publishing the snapshot.
 */
void audio_features_process(audio_features_ctx_t *ctx,
                            const int16_t *pcm,
                            size_t frames,
                            audio_levels_t *out_levels,
                            audio_features_t *out_features);
