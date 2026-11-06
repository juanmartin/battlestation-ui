#include "audio_analysis.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_format.h"
#include "audio_service.h"

#define AUDIO_ANALYSIS_PERIOD_MS 20
#define AUDIO_ANALYSIS_FRAMES    (AUDIO_FRAME_SAMPLES * 2)
#define AUDIO_ANALYSIS_STACK     6144
#define AUDIO_ANALYSIS_PRIORITY  8

static const char *TAG = "audio_analysis";

static audio_capture_reader_t s_reader;
static portMUX_TYPE s_levels_lock = portMUX_INITIALIZER_UNLOCKED;
static audio_levels_t s_levels;
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

static void audio_analysis_task(void *arg)
{
    (void)arg;

    audio_levels_ctx_t ctx;
    audio_features_levels_init(&ctx);

    int16_t block[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * 2];
    bool logged_error = false;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        esp_err_t err = read_analysis_block(s_reader, block);
        if (err == ESP_OK) {
            audio_levels_t levels;
            audio_features_process_levels(&ctx, block, AUDIO_FRAME_SAMPLES, &levels);
            audio_features_process_levels(&ctx,
                                          block + AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS,
                                          AUDIO_FRAME_SAMPLES,
                                          &levels);
            publish_levels(&levels);

            if ((ctx.frames % 100u) == 0) {
                ESP_LOGI(TAG, "L=%.1f R=%.1f avg=%.1f peak=%.1f dBFS",
                         levels.dbfs_l, levels.dbfs_r, levels.dbfs_avg, levels.peak_dbfs);
            }
            logged_error = false;
        } else {
            audio_service_status_t status = {
                .ready = false,
                .error = false,
            };
            bool report_error = true;
            if (audio_service_get_status(&status) == ESP_OK) {
                report_error = status.ready || status.error;
            }

            audio_levels_t levels;
            get_published_levels(&levels);
            if (report_error) {
                levels.error = true;
            }
            publish_levels(&levels);

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
