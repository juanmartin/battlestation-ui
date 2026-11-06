#include "audio_service_selftest.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_format.h"
#include "audio_service.h"

#define SELFTEST_TAG "audio_selftest"
#define SELFTEST_DELAY_MS 2000
#define TONE_FREQ_HZ 1000.0
#define TONE_AMPLITUDE 3276.0
#define TONE_BLOCKS 200
#define SILENCE_BLOCKS 20
#define TONE_TWO_PI 6.283185307179586476925286766559

static bool s_started;

static void audio_selftest_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(SELFTEST_DELAY_MS));

    audio_playback_config_t cfg = {
        .name = "test_tone",
        .priority = AUDIO_PRIO_TONE,
        .ring_ms = 80,
        .channels = 1,
        .gain_db = 0.0f,
    };
    audio_playback_source_t src = NULL;
    esp_err_t err = audio_service_playback_open(&cfg, &src);
    if (err != ESP_OK) {
        ESP_LOGE(SELFTEST_TAG, "tone lane open failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    err = audio_service_playback_set_active(src, true);
    if (err != ESP_OK) {
        ESP_LOGE(SELFTEST_TAG, "tone lane activate failed: %s", esp_err_to_name(err));
        audio_service_playback_close(src);
        vTaskDelete(NULL);
        return;
    }

    err = audio_service_set_output_muted(false);
    if (err != ESP_OK) {
        ESP_LOGE(SELFTEST_TAG, "unmute failed: %s", esp_err_to_name(err));
        audio_service_playback_set_active(src, false);
        audio_service_playback_close(src);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(SELFTEST_TAG, "start: 2 s, 1 kHz, -20 dBFS");

    int16_t block[AUDIO_FRAME_SAMPLES];
    double phase = 0.0;
    const double phase_step = TONE_TWO_PI * TONE_FREQ_HZ /
                              (double)AUDIO_SAMPLE_RATE_HZ;
    TickType_t last_wake = xTaskGetTickCount();
    bool write_failed = false;

    for (int b = 0; b < TONE_BLOCKS; b++) {
        for (size_t i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
            block[i] = (int16_t)lrint(TONE_AMPLITUDE * sin(phase));
            phase += phase_step;
            if (phase >= TONE_TWO_PI) {
                phase -= TONE_TWO_PI;
            }
        }

        size_t written = 0;
        err = audio_service_playback_write(src, block, AUDIO_FRAME_SAMPLES,
                                           50, &written);
        if ((err != ESP_OK || written != AUDIO_FRAME_SAMPLES) && !write_failed) {
            ESP_LOGW(SELFTEST_TAG, "tone write short: %s (%u/%u)",
                     esp_err_to_name(err), (unsigned)written,
                     (unsigned)AUDIO_FRAME_SAMPLES);
            write_failed = true;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(AUDIO_FRAME_MS));
    }

    memset(block, 0, sizeof(block));
    for (int b = 0; b < SILENCE_BLOCKS; b++) {
        size_t written = 0;
        err = audio_service_playback_write(src, block, AUDIO_FRAME_SAMPLES,
                                           50, &written);
        if ((err != ESP_OK || written != AUDIO_FRAME_SAMPLES) && !write_failed) {
            ESP_LOGW(SELFTEST_TAG, "silence write short: %s (%u/%u)",
                     esp_err_to_name(err), (unsigned)written,
                     (unsigned)AUDIO_FRAME_SAMPLES);
            write_failed = true;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(AUDIO_FRAME_MS));
    }

    audio_service_playback_set_active(src, false);
    audio_service_set_output_muted(true);
    audio_service_playback_close(src);

    ESP_LOGI(SELFTEST_TAG, "done");
    vTaskDelete(NULL);
}

esp_err_t audio_service_selftest_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(audio_selftest_task, "audio_selftest",
                                            4096, NULL, 4, NULL, 1);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}
