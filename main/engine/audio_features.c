#include "audio_features.h"

#include <math.h>

#include "audio_format.h"

#define DBFS_FLOOR (-96.0f)

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

    out->dbfs_l = ctx->smooth_l;
    out->dbfs_r = ctx->smooth_r;
    out->dbfs_avg = db_avg;
    out->peak_dbfs = ctx->peak_hold;
    out->frames = ctx->frames;
    out->running = true;
    out->error = false;
}
