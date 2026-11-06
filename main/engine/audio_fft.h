#pragma once

#include <stddef.h>
#include <stdint.h>

#define AUDIO_FFT_SIZE 1024u
#define AUDIO_FFT_BINS 512u

/*
 * Radix-2 complex FFT workspace reused for the 1024-point real transform.
 * Pure C: no ESP-IDF/FreeRTOS/LVGL dependencies.
 */
typedef struct {
    float window[AUDIO_FFT_SIZE];
    float re[AUDIO_FFT_SIZE];
    float im[AUDIO_FFT_SIZE];
    float twiddle_cos[AUDIO_FFT_SIZE / 2u];
    float twiddle_sin[AUDIO_FFT_SIZE / 2u];
    float rms_scale;
    float dc_scale;
    float power_scale;
} audio_fft_t;

void audio_fft_init(audio_fft_t *fft);

/*
 * Apply a periodic Hann window to normalized [-1, 1] mono samples and write
 * AUDIO_FFT_BINS one-sided dBFS values. A full-scale sine centered in a bin
 * reads about -3.01 dBFS; the floor is -120 dBFS.
 */
void audio_fft_spectrum_db(audio_fft_t *fft, const float *samples, float *spectrum_db);
