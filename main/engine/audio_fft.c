#include "audio_fft.h"

#include <math.h>

#define AUDIO_TWO_PI 6.283185307179586476925286766559

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

    double window_sum_sq = window_sum * window_sum;
    /* Raw |X[k]|^2 -> one-sided RMS power: full-scale sine = 0.5. */
    fft->bin_power_scale = (float)(2.0 / window_sum_sq);
    fft->dc_power_scale = (float)(1.0 / window_sum_sq);
    /* Remove the Hann noise-bandwidth gain when summing bins into bands. */
    fft->power_scale = (float)(window_sum_sq /
                               ((double)AUDIO_FFT_SIZE * window_energy));
}

void audio_fft_power(audio_fft_t *fft, const float *samples, float *power_out)
{
    if (fft == NULL || samples == NULL || power_out == NULL) {
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
        float raw_power = fft->re[k] * fft->re[k] + fft->im[k] * fft->im[k];
        float scale = (k == 0) ? fft->dc_power_scale : fft->bin_power_scale;
        power_out[k] = raw_power * scale;
    }
}
