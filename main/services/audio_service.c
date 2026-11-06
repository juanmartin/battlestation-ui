#include "audio_service.h"

#include <inttypes.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"

#include "audio_format.h"
#include "audio_ring.h"

#define AUDIO_READER_SLOTS 2
#define AUDIO_DEFAULT_RING_MS 200

static const char *TAG = "audio_service";

struct audio_capture_reader {
    audio_ring_t *ring;
    bool in_use;
};

static struct audio_capture_reader s_readers[AUDIO_READER_SLOTS];
static SemaphoreHandle_t s_readers_lock;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_service_ready;
static bool s_service_error;
static bool s_started;

static void audio_rx_task(void *arg)
{
    (void)arg;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .channel = AUDIO_CHANNELS,
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
    };

    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    if (mic == NULL) {
        ESP_LOGE(TAG, "ES7210 microphone init failed");
        portENTER_CRITICAL(&s_state_lock);
        s_service_error = true;
        portEXIT_CRITICAL(&s_state_lock);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = esp_codec_dev_open(mic, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %s", esp_err_to_name(ret));
        portENTER_CRITICAL(&s_state_lock);
        s_service_error = true;
        portEXIT_CRITICAL(&s_state_lock);
        vTaskDelete(NULL);
        return;
    }

    ret = esp_codec_dev_set_in_gain(mic, (float)CONFIG_APP_MIC_GAIN_DB);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mic gain set failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "ES7210 capture: %d Hz, %d ch, 16-bit, PGA %d dB",
             AUDIO_SAMPLE_RATE_HZ, AUDIO_CHANNELS, CONFIG_APP_MIC_GAIN_DB);

    portENTER_CRITICAL(&s_state_lock);
    s_service_ready = true;
    portEXIT_CRITICAL(&s_state_lock);

    int16_t frame[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
    bool logged_error = false;

    while (true) {
        ret = esp_codec_dev_read(mic, frame, (int)sizeof(frame));
        if (ret != ESP_OK) {
            if (!logged_error) {
                ESP_LOGE(TAG, "mic read failed: %s (will retry)", esp_err_to_name(ret));
                logged_error = true;
            }
            portENTER_CRITICAL(&s_state_lock);
            s_service_error = true;
            portEXIT_CRITICAL(&s_state_lock);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        portENTER_CRITICAL(&s_state_lock);
        s_service_error = false;
        portEXIT_CRITICAL(&s_state_lock);

        if (xSemaphoreTake(s_readers_lock, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < AUDIO_READER_SLOTS; i++) {
                if (s_readers[i].in_use && s_readers[i].ring != NULL) {
                    audio_ring_write(s_readers[i].ring, frame, AUDIO_FRAME_SAMPLES);
                }
            }
            xSemaphoreGive(s_readers_lock);
        }
    }
}

esp_err_t audio_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_readers_lock = xSemaphoreCreateMutex();
    if (s_readers_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(audio_rx_task, "audio_rx", 4096, NULL, 12, NULL, 0);
    if (ok != pdPASS) {
        vSemaphoreDelete(s_readers_lock);
        s_readers_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t audio_service_get_status(audio_service_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_state_lock);
    out->ready = s_service_ready;
    out->error = s_service_error;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

esp_err_t audio_service_capture_open(const audio_capture_config_t *cfg,
                                     audio_capture_reader_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    if (!s_started || s_readers_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t ring_ms = (cfg != NULL && cfg->ring_ms > 0) ? cfg->ring_ms : AUDIO_DEFAULT_RING_MS;
    uint64_t capacity64 = ((uint64_t)AUDIO_SAMPLE_RATE_HZ * ring_ms) / 1000;
    if (capacity64 == 0 || capacity64 > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    audio_ring_t *ring = audio_ring_create((uint32_t)capacity64);
    if (ring == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_readers_lock, portMAX_DELAY) != pdTRUE) {
        audio_ring_destroy(ring);
        return ESP_ERR_TIMEOUT;
    }

    struct audio_capture_reader *reader = NULL;
    for (int i = 0; i < AUDIO_READER_SLOTS; i++) {
        if (!s_readers[i].in_use) {
            reader = &s_readers[i];
            reader->ring = ring;
            reader->in_use = true;
            break;
        }
    }
    xSemaphoreGive(s_readers_lock);

    if (reader == NULL) {
        audio_ring_destroy(ring);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "capture reader '%s' open: %" PRIu32 " ms ring",
             (cfg != NULL && cfg->name != NULL) ? cfg->name : "unnamed", ring_ms);
    *out = reader;
    return ESP_OK;
}

esp_err_t audio_service_capture_read(audio_capture_reader_t reader,
                                     int16_t *dst,
                                     size_t max_frames,
                                     uint32_t timeout_ms,
                                     size_t *frames_read)
{
    if (reader == NULL || dst == NULL || frames_read == NULL || max_frames == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *frames_read = 0;

    if (!reader->in_use || reader->ring == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_state_lock);
    bool service_error = s_service_error;
    portEXIT_CRITICAL(&s_state_lock);
    if (service_error) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t frames = audio_ring_read(reader->ring, dst, max_frames);
    if (frames == 0 && timeout_ms > 0) {
        (void)audio_ring_wait(reader->ring, timeout_ms);
        frames = audio_ring_read(reader->ring, dst, max_frames);
    }

    *frames_read = frames;
    return frames > 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t audio_service_capture_close(audio_capture_reader_t reader)
{
    if (reader == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool valid = false;
    for (int i = 0; i < AUDIO_READER_SLOTS; i++) {
        if (&s_readers[i] == reader) {
            valid = true;
            break;
        }
    }
    if (!valid || s_readers_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    audio_ring_t *ring = NULL;
    if (xSemaphoreTake(s_readers_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (reader->in_use) {
        ring = reader->ring;
        reader->ring = NULL;
        reader->in_use = false;
    }
    xSemaphoreGive(s_readers_lock);

    if (ring == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    audio_ring_destroy(ring);
    return ESP_OK;
}
