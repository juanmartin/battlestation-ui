#include "audio_mixer.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int32_t gain_to_q15(float gain)
{
    if (!(gain > 0.0f)) {
        return 0;
    }
    float scaled = gain * 32768.0f;
    if (scaled > 65536.0f) {
        scaled = 65536.0f;
    }
    return (int32_t)lrintf(scaled);
}

static int32_t mix_add(int32_t accumulator, int32_t value)
{
    int64_t sum = (int64_t)accumulator + (int64_t)value;
    if (sum > INT32_MAX) {
        return INT32_MAX;
    }
    if (sum < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)sum;
}

void audio_mixer_expand_mono(const int16_t *mono, size_t frames, int16_t *stereo)
{
    if (mono == NULL || stereo == NULL) {
        return;
    }
    for (size_t i = 0; i < frames; i++) {
        int16_t sample = mono[i];
        stereo[i * 2 + 0] = sample;
        stereo[i * 2 + 1] = sample;
    }
}

void audio_mixer_process(const audio_mixer_lane_t *lanes, size_t lane_count,
                         int16_t *out, size_t frames)
{
    if (out == NULL || frames == 0) {
        return;
    }
    if (lanes == NULL || lane_count == 0) {
        memset(out, 0, frames * 2u * sizeof(int16_t));
        return;
    }

    size_t lanes_to_mix = lane_count;
    if (lanes_to_mix > AUDIO_MIXER_MAX_LANES) {
        lanes_to_mix = AUDIO_MIXER_MAX_LANES;
    }

    int32_t gains[AUDIO_MIXER_MAX_LANES];
    size_t offsets[AUDIO_MIXER_MAX_LANES];
    for (size_t i = 0; i < AUDIO_MIXER_MAX_LANES; i++) {
        if (i < lanes_to_mix) {
            gains[i] = gain_to_q15(lanes[i].gain);
        } else {
            gains[i] = 0;
        }
        offsets[i] = 0;
    }

    size_t out_index = 0;
    size_t remaining = frames;
    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > AUDIO_MIXER_CHUNK_FRAMES) {
            chunk = AUDIO_MIXER_CHUNK_FRAMES;
        }

        int32_t mix[AUDIO_MIXER_CHUNK_FRAMES * 2u];
        memset(mix, 0, chunk * 2u * sizeof(mix[0]));

        for (size_t i = 0; i < lanes_to_mix; i++) {
            if (!lanes[i].active || lanes[i].pcm == NULL || gains[i] == 0) {
                continue;
            }
            size_t available = 0;
            if (lanes[i].frames > offsets[i]) {
                available = lanes[i].frames - offsets[i];
            }
            size_t count = available < chunk ? available : chunk;
            const int16_t *pcm = lanes[i].pcm + offsets[i] * 2u;
            for (size_t j = 0; j < count; j++) {
                mix[j * 2u + 0u] = mix_add(mix[j * 2u + 0u],
                                           (int32_t)pcm[j * 2u + 0u] * gains[i]);
                mix[j * 2u + 1u] = mix_add(mix[j * 2u + 1u],
                                           (int32_t)pcm[j * 2u + 1u] * gains[i]);
            }
            offsets[i] += chunk;
        }

        for (size_t j = 0; j < chunk; j++) {
            int32_t left = mix[j * 2u + 0u] >> 15;
            int32_t right = mix[j * 2u + 1u] >> 15;
            if (left > 32767) {
                left = 32767;
            } else if (left < -32768) {
                left = -32768;
            }
            if (right > 32767) {
                right = 32767;
            } else if (right < -32768) {
                right = -32768;
            }
            out[out_index + j * 2u + 0u] = (int16_t)left;
            out[out_index + j * 2u + 1u] = (int16_t)right;
        }

        out_index += chunk * 2u;
        remaining -= chunk;
    }
}
