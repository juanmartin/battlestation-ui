#include "audio_features.h"
#include "audio_format.h"
#include "test_support.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_TWO_PI 6.283185307179586476925286766559
#define FULL_SCALE_SAMPLE 32767.0
#define SILENCE_DB (-96.0)

typedef struct {
    double freq_hz;
    double amp_l;
    double amp_r;
    uint64_t index;
} tone_t;

/*
 * Fill interleaved stereo int16 frames from a continuous, deterministic
 * sample index. amp 1.0 is a full-scale sine (32767 peak).
 */
static void tone_fill(tone_t *tone, int16_t *pcm, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        double n = (double)(tone->index + i);
        double phase = TEST_TWO_PI * tone->freq_hz * n /
                       (double)AUDIO_SAMPLE_RATE_HZ;
        double wave = sin(phase);
        pcm[i * AUDIO_CHANNELS + 0] =
            (int16_t)lrint(tone->amp_l * FULL_SCALE_SAMPLE * wave);
        pcm[i * AUDIO_CHANNELS + 1] =
            (int16_t)lrint(tone->amp_r * FULL_SCALE_SAMPLE * wave);
    }
    tone->index += frames;
}

static void process_blocks(audio_levels_ctx_t *ctx, tone_t *tone, size_t blocks,
                           audio_levels_t *out)
{
    int16_t pcm[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];

    for (size_t b = 0; b < blocks; b++) {
        tone_fill(tone, pcm, AUDIO_FRAME_SAMPLES);
        audio_features_process_levels(ctx, pcm, AUDIO_FRAME_SAMPLES, out);
    }
}

/* RMS dBFS of a full-scale sine: 20 log10((32767/sqrt(2))/32768). */
static double full_scale_sine_db(void)
{
    return 20.0 * log10((FULL_SCALE_SAMPLE / 32768.0) / sqrt(2.0));
}

static void test_silence_floor(void)
{
    audio_levels_ctx_t ctx;
    audio_levels_t out = {0};
    tone_t silence = {0};

    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &silence, 1, &out);

    CHECK_NEAR(out.dbfs_l, SILENCE_DB, 1e-4);
    CHECK_NEAR(out.dbfs_r, SILENCE_DB, 1e-4);
    CHECK_NEAR(out.dbfs_avg, SILENCE_DB, 1e-4);
    CHECK_NEAR(out.peak_dbfs, SILENCE_DB, 1e-4);
    CHECK_NEAR(out.leq_l, SILENCE_DB, 1e-4);
    CHECK_NEAR(out.leq_r, SILENCE_DB, 1e-4);
    CHECK(out.frames == 1);
    CHECK(out.running);
    CHECK(!out.error);
}

static void test_full_scale_sine_levels(void)
{
    audio_levels_ctx_t ctx;
    audio_levels_t out = {0};
    tone_t tone = {1000.0, 1.0, 1.0, 0};

    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &tone, 2, &out);

    double expected = full_scale_sine_db();
    CHECK_NEAR(out.dbfs_l, expected, 0.02);
    CHECK_NEAR(out.dbfs_r, expected, 0.02);
    CHECK_NEAR(out.dbfs_avg, expected, 0.02);

    /*
     * peak_dbfs is a sample-peak meter, so a full-scale sine reads ~0 dBFS
     * here even though its RMS level is -3.01 dBFS.
     */
    CHECK_NEAR(out.peak_dbfs, 20.0 * log10(FULL_SCALE_SAMPLE / 32768.0), 0.01);
}

static void test_peak_hold_three_dbfs_sample_peak(void)
{
    audio_levels_ctx_t ctx;
    audio_levels_t out = {0};
    /* Peak sample is -3.01 dBFS, matching the brief's peak-hold magnitude. */
    double amp = 1.0 / sqrt(2.0);
    tone_t tone = {1000.0, 0.0, 0.0, 0};

    tone.amp_l = amp;
    tone.amp_r = amp;
    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &tone, 1, &out);

    CHECK_NEAR(out.peak_dbfs, -3.0103, 0.02);
}

static void test_channel_independence(void)
{
    audio_levels_ctx_t ctx;
    audio_levels_t out = {0};
    tone_t left_only = {1000.0, 1.0, 0.0, 0};

    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &left_only, 3, &out);
    CHECK_NEAR(out.dbfs_l, full_scale_sine_db(), 0.02);
    CHECK_NEAR(out.dbfs_r, SILENCE_DB, 1e-4);
    CHECK_NEAR(out.leq_l, full_scale_sine_db(), 0.05);
    CHECK_NEAR(out.leq_r, SILENCE_DB, 1e-4);

    audio_levels_t out_r = {0};
    tone_t right_only = {1000.0, 0.0, 1.0, 0};
    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &right_only, 3, &out_r);
    CHECK_NEAR(out_r.dbfs_r, full_scale_sine_db(), 0.02);
    CHECK_NEAR(out_r.dbfs_l, SILENCE_DB, 1e-4);
    CHECK_NEAR(out_r.leq_r, full_scale_sine_db(), 0.05);
    CHECK_NEAR(out_r.leq_l, SILENCE_DB, 1e-4);
}

static void test_smoothing_trajectory(void)
{
    audio_levels_ctx_t ctx;
    audio_levels_t out = {0};
    tone_t silence = {0};
    tone_t tone = {1000.0, 1.0, 1.0, 0};

    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &silence, 1, &out);
    CHECK_NEAR(out.dbfs_l, SILENCE_DB, 1e-4);

    double target = full_scale_sine_db();
    for (int n = 1; n <= 20; n++) {
        process_blocks(&ctx, &tone, 1, &out);
        double expected = target + (SILENCE_DB - target) * pow(0.75, n);
        CHECK_NEAR(out.dbfs_l, expected, 0.02);
        CHECK_NEAR(out.dbfs_r, expected, 0.02);
    }
}

static void test_peak_hold_decay(void)
{
    audio_levels_ctx_t ctx;
    audio_levels_t out = {0};
    tone_t tone = {1000.0, 1.0, 1.0, 0};
    tone_t silence = {0};

    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &tone, 1, &out);
    CHECK_NEAR(out.peak_dbfs, 20.0 * log10(FULL_SCALE_SAMPLE / 32768.0), 0.01);

    double previous = out.peak_dbfs;
    for (int n = 0; n < 10; n++) {
        process_blocks(&ctx, &silence, 1, &out);
        CHECK_NEAR(out.peak_dbfs, previous - 0.05, 1e-4);
        previous = out.peak_dbfs;
    }
}

static void test_leq_window(void)
{
    audio_levels_ctx_t ctx;
    audio_levels_t out = {0};
    tone_t tone = {1000.0, 1.0, 1.0, 0};
    tone_t silence = {0};

    audio_features_levels_init(&ctx);
    process_blocks(&ctx, &tone, 110, &out);
    CHECK_NEAR(out.leq_l, full_scale_sine_db(), 0.05);
    CHECK_NEAR(out.leq_r, full_scale_sine_db(), 0.05);

    double before = out.leq_l;
    process_blocks(&ctx, &silence, 50, &out);

    /* 50 of the 100 one-second window slots are full scale, 50 are silent. */
    double full_power = pow(10.0, full_scale_sine_db() / 10.0);
    double silence_power = pow(10.0, SILENCE_DB / 10.0);
    double expected = 10.0 * log10(0.5 * full_power + 0.5 * silence_power);
    CHECK_NEAR(out.leq_l, expected, 0.05);
    CHECK_NEAR(out.leq_r, expected, 0.05);
    CHECK(out.leq_l < before - 2.0);
}

static void test_frame_counts(void)
{
    audio_features_ctx_t ctx;
    audio_levels_t levels = {0};
    audio_features_t features = {0};
    int16_t pcm[AUDIO_FRAME_SAMPLES * 2 * AUDIO_CHANNELS] = {0};

    audio_features_init(&ctx);
    audio_features_process(&ctx, pcm, AUDIO_FRAME_SAMPLES * 2, &levels,
                           &features);

    CHECK(levels.frames == 2);
    CHECK(features.levels.frames == 2);
    CHECK(features.fft_count == 0);
    CHECK(features.fft_size == AUDIO_FFT_SIZE);
    CHECK(features.fft_bins == AUDIO_FFT_BINS);
    CHECK_NEAR(features.bin_hz, 15.625, 1e-6);
}

static void test_fft_needs_full_history(void)
{
    audio_features_ctx_t ctx;
    audio_levels_t levels = {0};
    audio_features_t features = {0};
    tone_t tone = {1000.0, 1.0, 1.0, 0};
    int16_t pcm[AUDIO_FFT_SIZE * AUDIO_CHANNELS];
    int16_t one_frame[AUDIO_CHANNELS];

    tone_fill(&tone, pcm, AUDIO_FFT_SIZE - 1);
    audio_features_init(&ctx);
    audio_features_process(&ctx, pcm, AUDIO_FFT_SIZE - 1, &levels, &features);
    CHECK(features.fft_count == 0);
    CHECK(ctx.history_count == AUDIO_FFT_SIZE - 1);

    tone_fill(&tone, one_frame, 1);
    audio_features_process(&ctx, one_frame, 1, &levels, &features);
    CHECK(features.fft_count == 1);
    CHECK(ctx.history_count == AUDIO_FFT_SIZE);
}

int main(void)
{
    test_silence_floor();
    test_full_scale_sine_levels();
    test_peak_hold_three_dbfs_sample_peak();
    test_channel_independence();
    test_smoothing_trajectory();
    test_peak_hold_decay();
    test_leq_window();
    test_frame_counts();
    test_fft_needs_full_history();
    return test_report("test_features");
}
