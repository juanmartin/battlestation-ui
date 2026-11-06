#include "audio_analysis.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_format.h"
#include "audio_seqlock.h"
#include "audio_service.h"

#define AUDIO_ANALYSIS_PERIOD_MS 20
#define AUDIO_ANALYSIS_FRAMES    (AUDIO_FRAME_SAMPLES * 2)
#define AUDIO_ANALYSIS_STACK     6144
#define AUDIO_ANALYSIS_PRIORITY  8
#define AUDIO_SUMMARY_INTERVAL   250u /* 5 s at 20 ms publish cadence */

static const char *TAG = "audio_analysis";

static audio_capture_reader_t s_reader;
static portMUX_TYPE s_levels_lock = portMUX_INITIALIZER_UNLOCKED;
static audio_levels_t s_levels;
static audio_features_ctx_t s_features_ctx;
static audio_features_t s_features;
static audio_features_t s_features_work;
static audio_seqlock_t s_features_lock;
static uint32_t s_feature_seq;
static bool s_started;

static void publish_levels(const audio_levels_t *levels)
{
    portENTER_CRITICAL(&s_levels_lock);
    s_levels = *levels;
    portEXIT_CRITICAL(&s_levels_lock);
}

static void get_published_levels(audio_levels_t *levels)
{
    portENTER_CRITICAL(&s_levels_lock);
    *levels = s_levels;
    portEXIT_CRITICAL(&s_levels_lock);
}

static void publish_features(const audio_features_t *features)
{
    audio_seqlock_write_begin(&s_features_lock);
    s_features = *features;
    audio_seqlock_write_end(&s_features_lock);
}

static void init_features_snapshot(void)
{
    memset(&s_features, 0, sizeof(s_features));
    memset(&s_features_work, 0, sizeof(s_features_work));
    for (size_t k = 0; k < AUDIO_FFT_BINS; k++) {
        s_features.spectrum_db[k] = AUDIO_FEATURE_FLOOR_DB;
    }
    for (size_t b = 0; b < AUDIO_BAND_COUNT; b++) {
        s_features.band_db[b] = AUDIO_FEATURE_FLOOR_DB;
    }
    s_features.fft_size = AUDIO_FFT_SIZE;
    s_features.fft_bins = (uint16_t)AUDIO_FFT_BINS;
    s_features.bin_hz = (float)AUDIO_SAMPLE_RATE_HZ / (float)AUDIO_FFT_SIZE;
    s_feature_seq = 0;
    audio_seqlock_init(&s_features_lock);
}

static esp_err_t read_analysis_block(audio_capture_reader_t reader, int16_t *block)
{
    const TickType_t budget = pdMS_TO_TICKS(AUDIO_ANALYSIS_PERIOD_MS);
    TickType_t start = xTaskGetTickCount();
    size_t total = 0;

    while (total < AUDIO_ANALYSIS_FRAMES) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        uint32_t remaining_ms = 0;
        if (elapsed < budget) {
            remaining_ms = (uint32_t)((budget - elapsed) * portTICK_PERIOD_MS);
        }

        size_t frames = 0;
        esp_err_t err = audio_service_capture_read(reader,
                                                   block + total * AUDIO_CHANNELS,
                                                   AUDIO_ANALYSIS_FRAMES - total,
                                                   remaining_ms,
                                                   &frames);
        if (err != ESP_OK) {
            return err;
        }

        total += frames;
        if (frames == 0 || (total < AUDIO_ANALYSIS_FRAMES && remaining_ms == 0)) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

static void log_feature_summary(const audio_features_t *features, uint32_t max_us)
{
    uint16_t strongest_bin = 0;
    for (uint16_t i = 1; i < AUDIO_FFT_BINS; i++) {
        if (features->spectrum_db[i] > features->spectrum_db[strongest_bin]) {
            strongest_bin = i;
        }
    }

    uint16_t strongest_band = 0;
    for (uint16_t i = 1; i < AUDIO_BAND_COUNT; i++) {
        if (features->band_db[i] > features->band_db[strongest_band]) {
            strongest_band = i;
        }
    }

    ESP_LOGI(TAG,
             "dsp=%u us (max %u) strongest band=%u %.0f Hz %.1f dBFS bin=%u %.0f Hz %.1f dBFS",
             (unsigned)features->analysis_time_us, (unsigned)max_us,
             (unsigned)strongest_band,
             audio_features_band_centers_hz[strongest_band],
             features->band_db[strongest_band],
             (unsigned)strongest_bin,
             (float)strongest_bin * features->bin_hz,
             features->spectrum_db[strongest_bin]);
}

static void audio_analysis_task(void *arg)
{
    (void)arg;

    int16_t block[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * 2];
    bool logged_error = false;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t summary_count = 0;
    uint32_t summary_max_us = 0;

    while (true) {
        esp_err_t err = read_analysis_block(s_reader, block);
        if (err == ESP_OK) {
            audio_levels_t levels;
            int64_t start_us = esp_timer_get_time();
            audio_features_process(&s_features_ctx, block, AUDIO_ANALYSIS_FRAMES,
                                   &levels, &s_features_work);
            int64_t end_us = esp_timer_get_time();
            uint32_t analysis_us = (end_us > start_us) ? (uint32_t)(end_us - start_us) : 0;

            s_features_work.analysis_time_us = analysis_us;
            s_features_work.timestamp_us = (uint64_t)end_us;
            s_features_work.seq = ++s_feature_seq;

            publish_levels(&levels);
            publish_features(&s_features_work);

            if ((s_features_ctx.levels.frames % 100u) == 0) {
                ESP_LOGI(TAG, "L=%.1f R=%.1f avg=%.1f peak=%.1f dBFS",
                         levels.dbfs_l, levels.dbfs_r, levels.dbfs_avg, levels.peak_dbfs);
            }

            if (analysis_us > summary_max_us) {
                summary_max_us = analysis_us;
            }
            summary_count++;
            if (summary_count >= AUDIO_SUMMARY_INTERVAL) {
                log_feature_summary(&s_features_work, summary_max_us);
                summary_count = 0;
                summary_max_us = 0;
            }
            logged_error = false;
        } else {
            audio_service_status_t status = {
                .ready = false,
                .error = false,
            };
            bool report_error = true;
            if (audio_service_get_status(&status) == ESP_OK) {
                report_error = status.error || (s_features_ctx.levels.frames > 0);
            }

            audio_levels_t levels;
            get_published_levels(&levels);
            if (report_error) {
                levels.error = true;
            }
            publish_levels(&levels);

            s_features_work = s_features;
            s_features_work.levels = levels;
            s_features_work.analysis_time_us = 0;
            s_features_work.timestamp_us = (uint64_t)esp_timer_get_time();
            s_features_work.seq = ++s_feature_seq;
            publish_features(&s_features_work);

            if (report_error && !logged_error) {
                ESP_LOGW(TAG, "capture read failed: %s (will retry)", esp_err_to_name(err));
                logged_error = true;
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(AUDIO_ANALYSIS_PERIOD_MS));
    }
}

esp_err_t audio_analysis_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    audio_features_init(&s_features_ctx);
    init_features_snapshot();

    audio_capture_config_t cfg = {
        .name = "audio_analysis",
        .ring_ms = 200,
    };
    esp_err_t err = audio_service_capture_open(&cfg, &s_reader);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(audio_analysis_task, "audio_analysis",
                                            AUDIO_ANALYSIS_STACK, NULL,
                                            AUDIO_ANALYSIS_PRIORITY, NULL, 0);
    if (ok != pdPASS) {
        audio_service_capture_close(s_reader);
        s_reader = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t audio_analysis_get_levels(audio_levels_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    get_published_levels(out);
    return ESP_OK;
}

esp_err_t audio_analysis_get_features(audio_features_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* On timeout the caller must discard the buffer contents. */
    if (!audio_seqlock_read(&s_features_lock, out, &s_features,
                            sizeof(*out), 3)) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
