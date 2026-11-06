#include "audio_features.h"

#include <math.h>
#include <string.h>

#include "audio_format.h"

#define DBFS_FLOOR (-96.0f)

const float audio_features_band_centers_hz[AUDIO_BAND_COUNT] = {
    44.2f, 62.5f, 88.4f, 125.0f, 176.8f, 250.0f, 353.6f, 500.0f,
    707.0f, 1000.0f, 1414.0f, 2000.0f, 2828.0f, 4000.0f, 5657.0f, 8000.0f,
};

static float dbfs_from_rms(double sum_squares, int count)
{
    if (count <= 0) {
        return DBFS_FLOOR;
    }
    double rms = sqrt(sum_squares / (double)count);
    if (rms < 1.0) {
        return DBFS_FLOOR;
    }
    float db = 20.0f * log10f((float)(rms / 32768.0));
    return db < DBFS_FLOOR ? DBFS_FLOOR : db;
}

void audio_features_levels_init(audio_levels_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->have_smooth = false;
    ctx->smooth_l = DBFS_FLOOR;
    ctx->smooth_r = DBFS_FLOOR;
    ctx->peak_hold = DBFS_FLOOR;
    ctx->frames = 0;
    memset(ctx->leq_power_l, 0, sizeof(ctx->leq_power_l));
    memset(ctx->leq_power_r, 0, sizeof(ctx->leq_power_r));
    ctx->leq_index = 0;
    ctx->leq_count = 0;
    ctx->leq_sum_l = 0.0;
    ctx->leq_sum_r = 0.0;
}

void audio_features_process_levels(audio_levels_ctx_t *ctx,
                                   const int16_t *frame,
                                   size_t samples_per_channel,
                                   audio_levels_t *out)
{
    if (ctx == NULL || frame == NULL || out == NULL) {
        return;
    }

    double sum_l = 0.0;
    double sum_r = 0.0;
    int16_t peak = 0;
    for (size_t i = 0; i < samples_per_channel; i++) {
        int16_t l = frame[i * AUDIO_CHANNELS + 0];
        int16_t r = frame[i * AUDIO_CHANNELS + 1];
        sum_l += (double)l * (double)l;
        sum_r += (double)r * (double)r;
        int32_t a = l < 0 ? -(int32_t)l : (int32_t)l;
        if (a > peak) {
            peak = (int16_t)a;
        }
        a = r < 0 ? -(int32_t)r : (int32_t)r;
        if (a > peak) {
            peak = (int16_t)a;
        }
    }

    float db_l = dbfs_from_rms(sum_l, (int)samples_per_channel);
    float db_r = dbfs_from_rms(sum_r, (int)samples_per_channel);
    float db_avg = 10.0f * log10f(
        (powf(10.0f, db_l / 10.0f) + powf(10.0f, db_r / 10.0f)) / 2.0f);
    if (db_avg < DBFS_FLOOR) {
        db_avg = DBFS_FLOOR;
    }

    if (!ctx->have_smooth) {
        ctx->smooth_l = db_l;
        ctx->smooth_r = db_r;
        ctx->have_smooth = true;
    } else {
        ctx->smooth_l = ctx->smooth_l * 0.75f + db_l * 0.25f;
        ctx->smooth_r = ctx->smooth_r * 0.75f + db_r * 0.25f;
    }

    float peak_db = peak > 0 ? 20.0f * log10f((float)peak / 32768.0f) : DBFS_FLOOR;
    if (peak_db < DBFS_FLOOR) {
        peak_db = DBFS_FLOOR;
    }
    ctx->peak_hold = fmaxf(ctx->peak_hold - 0.05f, peak_db);
    ctx->frames++;

    /* 1 s sliding Leq: 100 blocks of 10 ms per channel. */
    float power_l = powf(10.0f, db_l / 10.0f);
    float power_r = powf(10.0f, db_r / 10.0f);
    uint32_t idx = ctx->leq_index;
    if (ctx->leq_count < AUDIO_LEQ_BLOCKS) {
        ctx->leq_count++;
    } else {
        ctx->leq_sum_l -= ctx->leq_power_l[idx];
        ctx->leq_sum_r -= ctx->leq_power_r[idx];
    }
    ctx->leq_power_l[idx] = power_l;
    ctx->leq_power_r[idx] = power_r;
    ctx->leq_sum_l += power_l;
    ctx->leq_sum_r += power_r;
    ctx->leq_index = (idx + 1u) % AUDIO_LEQ_BLOCKS;

    float leq_l = DBFS_FLOOR;
    float leq_r = DBFS_FLOOR;
    if (ctx->leq_count > 0) {
        float mean_l = (float)(ctx->leq_sum_l / (double)ctx->leq_count);
        float mean_r = (float)(ctx->leq_sum_r / (double)ctx->leq_count);
        if (mean_l > 0.0f) {
            leq_l = 10.0f * log10f(mean_l);
            if (leq_l < DBFS_FLOOR) {
                leq_l = DBFS_FLOOR;
            }
        }
        if (mean_r > 0.0f) {
            leq_r = 10.0f * log10f(mean_r);
            if (leq_r < DBFS_FLOOR) {
                leq_r = DBFS_FLOOR;
            }
        }
    }

    out->dbfs_l = ctx->smooth_l;
    out->dbfs_r = ctx->smooth_r;
    out->dbfs_avg = db_avg;
    out->peak_dbfs = ctx->peak_hold;
    out->leq_l = leq_l;
    out->leq_r = leq_r;
    out->frames = ctx->frames;
    out->running = true;
    out->error = false;
}

void audio_features_init(audio_features_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    audio_features_levels_init(&ctx->levels);
    audio_fft_init(&ctx->fft);
    memset(ctx->history, 0, sizeof(ctx->history));
    memset(ctx->fft_input, 0, sizeof(ctx->fft_input));
    ctx->history_count = 0;
    ctx->history_pos = 0;
    ctx->fft_count = 0;

    ctx->band_edges[0] = audio_features_band_centers_hz[0] / sqrtf(2.0f);
    for (size_t i = 1; i < AUDIO_BAND_COUNT; i++) {
        ctx->band_edges[i] = sqrtf(audio_features_band_centers_hz[i - 1] *
                                   audio_features_band_centers_hz[i]);
    }
    ctx->band_edges[AUDIO_BAND_COUNT] = (float)AUDIO_SAMPLE_RATE_HZ / 2.0f;
}

void audio_features_process(audio_features_ctx_t *ctx,
                            const int16_t *pcm,
                            size_t frames,
                            audio_levels_t *out_levels,
                            audio_features_t *out_features)
{
    if (ctx == NULL || pcm == NULL || frames == 0 ||
        out_levels == NULL || out_features == NULL) {
        return;
    }

    size_t offset = 0;
    while (offset + AUDIO_FRAME_SAMPLES <= frames) {
        audio_features_process_levels(&ctx->levels,
                                      pcm + offset * AUDIO_CHANNELS,
                                      AUDIO_FRAME_SAMPLES,
                                      out_levels);
        offset += AUDIO_FRAME_SAMPLES;
    }

    for (size_t i = 0; i < frames; i++) {
        float mono = ((float)pcm[i * AUDIO_CHANNELS + 0] +
                      (float)pcm[i * AUDIO_CHANNELS + 1]) * 0.5f / 32768.0f;
        ctx->history[ctx->history_pos] = mono;
        ctx->history_pos++;
        if (ctx->history_pos >= AUDIO_FFT_SIZE) {
            ctx->history_pos = 0;
        }
        if (ctx->history_count < AUDIO_FFT_SIZE) {
            ctx->history_count++;
        }
    }

    out_features->fft_size = AUDIO_FFT_SIZE;
    out_features->fft_bins = (uint16_t)AUDIO_FFT_BINS;
    out_features->bin_hz = (float)AUDIO_SAMPLE_RATE_HZ / (float)AUDIO_FFT_SIZE;

    for (size_t k = 0; k < AUDIO_FFT_BINS; k++) {
        out_features->spectrum_db[k] = AUDIO_FEATURE_FLOOR_DB;
    }
    for (size_t b = 0; b < AUDIO_BAND_COUNT; b++) {
        out_features->band_db[b] = AUDIO_FEATURE_FLOOR_DB;
    }

    if (ctx->history_count >= AUDIO_FFT_SIZE) {
        size_t start = ctx->history_pos;
        for (size_t i = 0; i < AUDIO_FFT_SIZE; i++) {
            ctx->fft_input[i] = ctx->history[(start + i) % AUDIO_FFT_SIZE];
        }
        audio_fft_spectrum_db(&ctx->fft, ctx->fft_input, out_features->spectrum_db);
        ctx->fft_count++;
    }
    out_features->fft_count = ctx->fft_count;

    for (size_t b = 0; b < AUDIO_BAND_COUNT; b++) {
        double power = 0.0;
        for (size_t k = 0; k < AUDIO_FFT_BINS; k++) {
            float freq = (float)k * out_features->bin_hz;
            if (freq < ctx->band_edges[b] || freq >= ctx->band_edges[b + 1u]) {
                continue;
            }
            if (out_features->spectrum_db[k] <= AUDIO_FEATURE_FLOOR_DB) {
                continue;
            }
            power += (double)powf(10.0f, out_features->spectrum_db[k] / 10.0f);
        }
        if (power > 0.0) {
            float db = 10.0f * log10f((float)(power * (double)ctx->fft.power_scale));
            if (db < AUDIO_FEATURE_FLOOR_DB) {
                db = AUDIO_FEATURE_FLOOR_DB;
            }
            out_features->band_db[b] = db;
        }
    }

    out_features->levels = *out_levels;
}
