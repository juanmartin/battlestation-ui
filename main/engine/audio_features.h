#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Rolling state for the 10 ms level processor. */
typedef struct {
    bool have_smooth;
    float smooth_l;
    float smooth_r;
    float peak_hold;
    uint32_t frames;
} audio_levels_ctx_t;

/* Published levels snapshot; field names match the legacy probe snapshot. */
typedef struct {
    float dbfs_l;
    float dbfs_r;
    float dbfs_avg;
    float peak_dbfs;
    uint32_t frames;
    bool running;
    bool error;
} audio_levels_t;

void audio_features_levels_init(audio_levels_ctx_t *ctx);

/*
 * Process one block of AUDIO_FRAME_SAMPLES frames of interleaved stereo
 * samples. Pure C: no logs, no ESP-IDF/FreeRTOS/LVGL dependencies.
 */
void audio_features_process_levels(audio_levels_ctx_t *ctx,
                                   const int16_t *frame,
                                   size_t samples_per_channel,
                                   audio_levels_t *out);
