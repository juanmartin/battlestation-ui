#pragma once

#include <stdint.h>

/* Fixed audio format shared by capture and analysis (ADR-0008). */
#define AUDIO_SAMPLE_RATE_HZ 16000
#define AUDIO_CHANNELS       2
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_FRAME_MS       10
#define AUDIO_FRAME_SAMPLES  (AUDIO_SAMPLE_RATE_HZ / (1000 / AUDIO_FRAME_MS))
#define AUDIO_FRAME_BYTES    (AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * sizeof(int16_t))
