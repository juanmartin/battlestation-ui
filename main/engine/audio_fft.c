#include "audio_fft.h"

#include <math.h>

#define AUDIO_TWO_PI 6.283185307179586476925286766559

#define SPECTRUM_FLOOR_DB (-120.0f)

void audio_fft_init(audio_fft_t *fft)
{
    if (fft == NULL) {
        return;
    }

    double window_sum = 0.0;
    double window_energy = 0.0;
    for (size_t i = 0; i < AUDIO_FFT_SIZE; i++) {
        double phase = AUDIO_TWO_PI * (double)i / (double)AUDIO_FFT_SIZE;
        float w = (float)(0.5 - 0.5 * cos(phase));
        fft->window[i] = w;
        window_sum += w;
        window_energy += (double)w * (double)w;
    }

    for (size_t k = 0; k < AUDIO_FFT_SIZE / 2u; k++) {
        double phase = AUDIO_TWO_PI * (double)k / (double)AUDIO_FFT_SIZE;
        fft->twiddle_cos[k] = (float)cos(phase);
        fft->twiddle_sin[k] = (float)-sin(phase);
    }

    /* One-sided RMS scaling: a full-scale sine reads -3.01 dBFS. */
    fft->rms_scale = (float)(sqrt(2.0) / window_sum);
    fft->dc_scale = (float)(1.0 / window_sum);
    /* Remove the Hann noise-bandwidth gain when summing bins into bands. */
    fft->power_scale = (float)((window_sum * window_sum) /
                               ((double)AUDIO_FFT_SIZE * window_energy));
}

void audio_fft_spectrum_db(audio_fft_t *fft, const float *samples, float *spectrum_db)
{
    if (fft == NULL || samples == NULL || spectrum_db == NULL) {
        return;
    }

    for (size_t i = 0; i < AUDIO_FFT_SIZE; i++) {
        fft->re[i] = samples[i] * fft->window[i];
        fft->im[i] = 0.0f;
    }

    /* Bit-reversal permutation. */
    for (size_t i = 1, j = 0; i < AUDIO_FFT_SIZE; i++) {
        size_t bit = AUDIO_FFT_SIZE >> 1u;
        for (; (j & bit) != 0; bit >>= 1u) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float tr = fft->re[i];
            fft->re[i] = fft->re[j];
            fft->re[j] = tr;
            float ti = fft->im[i];
            fft->im[i] = fft->im[j];
            fft->im[j] = ti;
        }
    }

    /* Iterative radix-2 butterflies with precomputed twiddles. */
    for (size_t len = 2; len <= AUDIO_FFT_SIZE; len <<= 1u) {
        size_t half = len >> 1u;
        size_t step = AUDIO_FFT_SIZE / len;
        for (size_t base = 0; base < AUDIO_FFT_SIZE; base += len) {
            for (size_t j = 0; j < half; j++) {
                float wr = fft->twiddle_cos[j * step];
                float wi = fft->twiddle_sin[j * step];
                size_t a = base + j;
                size_t b = a + half;
                float tr = wr * fft->re[b] - wi * fft->im[b];
                float ti = wr * fft->im[b] + wi * fft->re[b];
                fft->re[b] = fft->re[a] - tr;
                fft->im[b] = fft->im[a] - ti;
                fft->re[a] += tr;
                fft->im[a] += ti;
            }
        }
    }

    for (size_t k = 0; k < AUDIO_FFT_BINS; k++) {
        float magnitude = sqrtf(fft->re[k] * fft->re[k] + fft->im[k] * fft->im[k]);
        float scale = (k == 0) ? fft->dc_scale : fft->rms_scale;
        float amplitude = magnitude * scale;
        float db = SPECTRUM_FLOOR_DB;
        if (amplitude > 0.0f) {
            db = 20.0f * log10f(amplitude);
            if (db < SPECTRUM_FLOOR_DB) {
                db = SPECTRUM_FLOOR_DB;
            }
        }
        spectrum_db[k] = db;
    }
}
