#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_MIXER_MAX_LANES 3u
#define AUDIO_MIXER_CHUNK_FRAMES 160u

/*
 * One playback lane. PCM is interleaved stereo, frames is the number of valid
 * frames (short lanes are zero-padded by the mixer), gain is linear, and
 * inactive lanes are ignored.
 */
typedef struct {
    const int16_t *pcm;
    size_t frames;
    float gain;
    bool active;
} audio_mixer_lane_t;

/* Duplicate mono samples into interleaved stereo. */
void audio_mixer_expand_mono(const int16_t *mono, size_t frames, int16_t *stereo);

/*
 * Mix up to AUDIO_MIXER_MAX_LANES stereo lanes into out. Sums in saturating
 * int32, applies per-lane linear gain, and clips to int16. Pure C.
 */
void audio_mixer_process(const audio_mixer_lane_t *lanes, size_t lane_count,
                         int16_t *out, size_t frames);
