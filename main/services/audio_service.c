#include "audio_service.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"

#include "audio_format.h"

#define DBFS_FLOOR (-96.0f)

static const char *TAG = "audio_probe";

static esp_codec_dev_handle_t s_mic;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static audio_service_legacy_snapshot_t s_snapshot;

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

static void audio_task(void *arg)
{
    (void)arg;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .channel = AUDIO_CHANNELS,
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
    };

    s_mic = bsp_audio_codec_microphone_init();
    if (s_mic == NULL) {
        ESP_LOGE(TAG, "ES7210 microphone init failed");
        portENTER_CRITICAL(&s_lock);
        s_snapshot.error = true;
        portEXIT_CRITICAL(&s_lock);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = esp_codec_dev_open(s_mic, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %s", esp_err_to_name(ret));
        portENTER_CRITICAL(&s_lock);
        s_snapshot.error = true;
        portEXIT_CRITICAL(&s_lock);
        vTaskDelete(NULL);
        return;
    }

    ret = esp_codec_dev_set_in_gain(s_mic, (float)CONFIG_APP_MIC_GAIN_DB);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mic gain set failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "ES7210 capture: %d Hz, %d ch, 16-bit, PGA %d dB",
             AUDIO_SAMPLE_RATE_HZ, AUDIO_CHANNELS, CONFIG_APP_MIC_GAIN_DB);

    int16_t frame[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
    bool have_smooth = false;
    float smooth_l = DBFS_FLOOR;
    float smooth_r = DBFS_FLOOR;
    float peak_hold = DBFS_FLOOR;
    bool logged_error = false;
    uint32_t log_frames = 0;

    portENTER_CRITICAL(&s_lock);
    s_snapshot.running = true;
    portEXIT_CRITICAL(&s_lock);

    while (true) {
        ret = esp_codec_dev_read(s_mic, frame, (int)sizeof(frame));
        if (ret != ESP_OK) {
            if (!logged_error) {
                ESP_LOGE(TAG, "mic read failed: %s (will retry)", esp_err_to_name(ret));
                logged_error = true;
            }
            portENTER_CRITICAL(&s_lock);
            s_snapshot.error = true;
            portEXIT_CRITICAL(&s_lock);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        double sum_l = 0.0;
        double sum_r = 0.0;
        int16_t peak = 0;
        for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
            int16_t l = frame[i * 2 + 0];
            int16_t r = frame[i * 2 + 1];
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

        float db_l = dbfs_from_rms(sum_l, AUDIO_FRAME_SAMPLES);
        float db_r = dbfs_from_rms(sum_r, AUDIO_FRAME_SAMPLES);
        float db_avg = 10.0f * log10f(
            (powf(10.0f, db_l / 10.0f) + powf(10.0f, db_r / 10.0f)) / 2.0f);
        if (db_avg < DBFS_FLOOR) {
            db_avg = DBFS_FLOOR;
        }

        if (!have_smooth) {
            smooth_l = db_l;
            smooth_r = db_r;
            have_smooth = true;
        } else {
            smooth_l = smooth_l * 0.75f + db_l * 0.25f;
            smooth_r = smooth_r * 0.75f + db_r * 0.25f;
        }

        float peak_db = peak > 0 ? 20.0f * log10f((float)peak / 32768.0f) : DBFS_FLOOR;
        if (peak_db < DBFS_FLOOR) {
            peak_db = DBFS_FLOOR;
        }
        peak_hold = fmaxf(peak_hold - 0.05f, peak_db);

        portENTER_CRITICAL(&s_lock);
        s_snapshot.dbfs_l = smooth_l;
        s_snapshot.dbfs_r = smooth_r;
        s_snapshot.dbfs_avg = db_avg;
        s_snapshot.peak_dbfs = peak_hold;
        s_snapshot.frames++;
        s_snapshot.running = true;
        s_snapshot.error = false;
        portEXIT_CRITICAL(&s_lock);

        log_frames++;
        if ((log_frames % 100) == 0) {
            ESP_LOGI(TAG, "L=%.1f R=%.1f avg=%.1f peak=%.1f dBFS",
                     smooth_l, smooth_r, db_avg, peak_hold);
        }
    }
}

esp_err_t audio_service_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "audio_probe", 4096, NULL, 8, NULL, 0);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void audio_service_get_legacy(audio_service_legacy_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
}
