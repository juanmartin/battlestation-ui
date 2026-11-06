#include "audio_fft.h"
#include "audio_features.h"
#include "audio_format.h"
#include "test_support.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_TWO_PI 6.283185307179586476925286766559

static void fill_sine_f32(float *out, size_t count, double freq_hz)
{
    for (size_t i = 0; i < count; i++) {
        double phase = TEST_TWO_PI * freq_hz * (double)i /
                       (double)AUDIO_SAMPLE_RATE_HZ;
        out[i] = (float)sin(phase);
    }
}

/* Mono downmix of an equal-channel, full-scale int16 sine. */
static void fill_sine_i16_stereo(int16_t *out, size_t frames, double freq_hz)
{
    for (size_t i = 0; i < frames; i++) {
        double phase = TEST_TWO_PI * freq_hz * (double)i /
                       (double)AUDIO_SAMPLE_RATE_HZ;
        int16_t sample = (int16_t)lrint(32767.0 * sin(phase));
        out[i * AUDIO_CHANNELS + 0] = sample;
        out[i * AUDIO_CHANNELS + 1] = sample;
    }
}

static size_t argmax_f32(const float *values, size_t count)
{
    size_t best = 0;
    for (size_t i = 1; i < count; i++) {
        if (values[i] > values[best]) {
            best = i;
        }
    }
    return best;
}

static double power_dbfs(float power)
{
    return 10.0 * log10((double)power);
}

static void test_sine_peak_bin(double freq_hz, size_t expected_bin)
{
    audio_fft_t fft;
    float samples[AUDIO_FFT_SIZE];
    float power[AUDIO_FFT_BINS];

    audio_fft_init(&fft);
    fill_sine_f32(samples, AUDIO_FFT_SIZE, freq_hz);
    audio_fft_power(&fft, samples, power);

    size_t peak = argmax_f32(power, AUDIO_FFT_BINS);
    CHECK(peak == expected_bin);
    CHECK_NEAR(power[peak], 0.5, 0.01);
    CHECK_NEAR(power_dbfs(power[peak]), -3.0103, 0.1);

    /* A bin-centred Hann-windowed sine puts ~-6 dB in the adjacent bins. */
    if (peak > 0) {
        CHECK(power[peak - 1] < power[peak] * 0.3f);
    }
    if (peak + 1 < AUDIO_FFT_BINS) {
        CHECK(power[peak + 1] < power[peak] * 0.3f);
    }
}

static size_t strongest_band(const audio_features_t *features)
{
    size_t best = 0;
    for (size_t b = 1; b < AUDIO_BAND_COUNT; b++) {
        if (features->band_db[b] > features->band_db[best]) {
            best = b;
        }
    }
    return best;
}

static size_t band_of_tone(double freq_hz)
{
    audio_features_ctx_t ctx;
    audio_levels_t levels = {0};
    audio_features_t features = {0};
    int16_t pcm[AUDIO_FFT_SIZE * AUDIO_CHANNELS];

    fill_sine_i16_stereo(pcm, AUDIO_FFT_SIZE, freq_hz);
    audio_features_init(&ctx);
    audio_features_process(&ctx, pcm, AUDIO_FFT_SIZE, &levels, &features);

    CHECK(features.fft_count == 1);
    return strongest_band(&features);
}

static void test_half_octave_bands(void)
{
    CHECK_NEAR(audio_features_band_centers_hz[2], 88.4, 0.05);

    /* Low E (82.41 Hz) and 100 Hz belong to the 88.4 Hz band. */
    CHECK(band_of_tone(82.41) == 2);
    CHECK(band_of_tone(100.0) == 2);

    /* 1 kHz and 4 kHz land in their own bands, not the low-E band. */
    size_t band_1k = band_of_tone(1000.0);
    size_t band_4k = band_of_tone(4000.0);
    CHECK(band_1k == 9);
    CHECK(band_4k == 13);
    CHECK(band_1k != 2);
    CHECK(band_4k != 2);
}

static void test_silence_spectrum_floor(void)
{
    audio_features_ctx_t ctx;
    audio_levels_t levels = {0};
    audio_features_t features = {0};
    int16_t pcm[AUDIO_FFT_SIZE * AUDIO_CHANNELS] = {0};

    audio_features_init(&ctx);
    audio_features_process(&ctx, pcm, AUDIO_FFT_SIZE, &levels, &features);

    CHECK(features.fft_count == 1);
    for (size_t k = 0; k < AUDIO_FFT_BINS; k++) {
        CHECK_NEAR(features.spectrum_db[k], AUDIO_FEATURE_FLOOR_DB, 1e-4);
    }
    for (size_t b = 0; b < AUDIO_BAND_COUNT; b++) {
        CHECK_NEAR(features.band_db[b], AUDIO_FEATURE_FLOOR_DB, 1e-4);
    }
}

int main(void)
{
    /* 16000 Hz / 1024 = 15.625 Hz bins: 1 kHz = bin 64, 4 kHz = bin 256. */
    test_sine_peak_bin(1000.0, 64);
    test_sine_peak_bin(4000.0, 256);
    test_half_octave_bands();
    test_silence_spectrum_floor();
    return test_report("test_fft");
}
