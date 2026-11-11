#include "audio_service.h"

#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"

#include "audio_format.h"
#include "audio_mixer.h"
#include "audio_ring.h"

#define AUDIO_READER_SLOTS 2
#define AUDIO_DEFAULT_RING_MS 200
#define AUDIO_PLAYBACK_LANES 3
#define AUDIO_PLAYBACK_DEFAULT_RING_MS 80
#define AUDIO_DEFAULT_OUTPUT_VOLUME 60

static const char *TAG = "audio_service";

struct audio_capture_reader {
    audio_ring_t *ring;
    bool in_use;
};

struct audio_playback_source {
    audio_ring_t *ring;
    audio_playback_priority_t priority;
    uint8_t channels;
    float trim_gain;
    bool active;
    bool in_use;
};

/*
 * Ducking table: linear gain by [highest active priority][lane priority].
 * -9 dB = 0.354813, -15 dB = 0.177828. Entries above the highest active
 * priority are unreachable and kept at zero.
 */
static const float s_duck_gain[AUDIO_PLAYBACK_LANES][AUDIO_PLAYBACK_LANES] = {
    { 1.0f, 0.0f, 0.0f },                      /* highest notification */
    { 0.35481339f, 1.0f, 0.0f },               /* highest metronome */
    { 0.17782794f, 0.35481339f, 1.0f },        /* highest tone */
};

static struct audio_capture_reader s_readers[AUDIO_READER_SLOTS];
static struct audio_playback_source s_playback[AUDIO_PLAYBACK_LANES];
static SemaphoreHandle_t s_readers_lock;
static SemaphoreHandle_t s_playback_lock;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static bool s_service_ready;
static bool s_service_error;
static bool s_playback_ready;
static bool s_output_muted = true;
static int s_output_volume = AUDIO_DEFAULT_OUTPUT_VOLUME;
static uint32_t s_tx_frames;
static uint32_t s_tx_underruns;
static bool s_started;

static int16_t s_tx_lane_pcm[AUDIO_PLAYBACK_LANES][AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
static int16_t s_tx_out[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];

static bool playback_source_in_pool(audio_playback_source_t src)
{
    for (int i = 0; i < AUDIO_PLAYBACK_LANES; i++) {
        if (&s_playback[i] == src) {
            return true;
        }
    }
    return false;
}

static void audio_rx_task(void *arg)
{
    (void)arg;

    int16_t frame[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
    bool logged_error = false;

    while (true) {
        int ret = esp_codec_dev_read(s_mic, frame, (int)sizeof(frame));
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

static void audio_tx_task(void *arg)
{
    (void)arg;

    bool logged_write_error = false;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        audio_mixer_lane_t lanes[AUDIO_PLAYBACK_LANES];
        float trim_gain[AUDIO_PLAYBACK_LANES];
        int lane_priority[AUDIO_PLAYBACK_LANES];
        memset(lanes, 0, sizeof(lanes));
        memset(trim_gain, 0, sizeof(trim_gain));
        for (int i = 0; i < AUDIO_PLAYBACK_LANES; i++) {
            lane_priority[i] = -1;
        }

        int highest_priority = -1;
        uint32_t underruns = 0;

        if (xSemaphoreTake(s_playback_lock, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < AUDIO_PLAYBACK_LANES; i++) {
                struct audio_playback_source *src = &s_playback[i];
                if (!src->in_use || !src->active || src->ring == NULL) {
                    continue;
                }

                size_t frames = audio_ring_read(src->ring, s_tx_lane_pcm[i],
                                                AUDIO_FRAME_SAMPLES);
                lanes[i].pcm = s_tx_lane_pcm[i];
                lanes[i].frames = frames;
                lanes[i].active = true;
                trim_gain[i] = src->trim_gain;
                lane_priority[i] = (int)src->priority;
                if (lane_priority[i] > highest_priority) {
                    highest_priority = lane_priority[i];
                }
                if (frames < AUDIO_FRAME_SAMPLES) {
                    underruns++;
                }
            }
            xSemaphoreGive(s_playback_lock);
        }

        if (highest_priority >= 0) {
            for (int i = 0; i < AUDIO_PLAYBACK_LANES; i++) {
                if (lanes[i].active) {
                    lanes[i].gain = trim_gain[i] *
                                    s_duck_gain[highest_priority][lane_priority[i]];
                }
            }
            audio_mixer_process(lanes, AUDIO_PLAYBACK_LANES, s_tx_out,
                                AUDIO_FRAME_SAMPLES);
        } else {
            memset(s_tx_out, 0, sizeof(s_tx_out));
        }

        if (underruns > 0) {
            portENTER_CRITICAL(&s_state_lock);
            s_tx_underruns += underruns;
            portEXIT_CRITICAL(&s_state_lock);
        }

        if (s_spk != NULL && s_playback_ready) {
            int ret = esp_codec_dev_write(s_spk, s_tx_out, (int)sizeof(s_tx_out));
            if (ret == ESP_CODEC_DEV_OK) {
                portENTER_CRITICAL(&s_state_lock);
                s_tx_frames += AUDIO_FRAME_SAMPLES;
                portEXIT_CRITICAL(&s_state_lock);
            } else if (!logged_write_error) {
                ESP_LOGE(TAG, "speaker write failed: %d (will retry)", ret);
                logged_write_error = true;
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(AUDIO_FRAME_MS));
    }
}

static void audio_service_delete_locks(void)
{
    if (s_readers_lock != NULL) {
        vSemaphoreDelete(s_readers_lock);
        s_readers_lock = NULL;
    }
    if (s_playback_lock != NULL) {
        vSemaphoreDelete(s_playback_lock);
        s_playback_lock = NULL;
    }
}

static void audio_service_delete_codecs(void)
{
    if (s_mic != NULL) {
        esp_codec_dev_delete(s_mic);
        s_mic = NULL;
    }
    if (s_spk != NULL) {
        esp_codec_dev_delete(s_spk);
        s_spk = NULL;
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
    s_playback_lock = xSemaphoreCreateMutex();
    if (s_playback_lock == NULL) {
        audio_service_delete_locks();
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .channel = AUDIO_CHANNELS,
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
    };

    s_mic = bsp_audio_codec_microphone_init();
    if (s_mic == NULL) {
        ESP_LOGE(TAG, "ES7210 microphone init failed");
        portENTER_CRITICAL(&s_state_lock);
        s_service_error = true;
        s_service_ready = false;
        portEXIT_CRITICAL(&s_state_lock);
        audio_service_delete_locks();
        return ESP_FAIL;
    }

    int ret = esp_codec_dev_open(s_mic, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "microphone open failed: %s", esp_err_to_name(ret));
        esp_codec_dev_delete(s_mic);
        s_mic = NULL;
        portENTER_CRITICAL(&s_state_lock);
        s_service_error = true;
        s_service_ready = false;
        portEXIT_CRITICAL(&s_state_lock);
        audio_service_delete_locks();
        return ESP_FAIL;
    }

    ret = esp_codec_dev_set_in_gain(s_mic, (float)CONFIG_APP_MIC_GAIN_DB);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mic gain set failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "ES7210 capture: %d Hz, %d ch, 16-bit, PGA %d dB",
             AUDIO_SAMPLE_RATE_HZ, AUDIO_CHANNELS, CONFIG_APP_MIC_GAIN_DB);

    portENTER_CRITICAL(&s_state_lock);
    s_service_ready = true;
    s_service_error = false;
    portEXIT_CRITICAL(&s_state_lock);

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        ESP_LOGE(TAG, "ES8311 speaker init failed; capture continues");
    } else {
        ret = esp_codec_dev_open(s_spk, &fs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "speaker open failed: %s; capture continues",
                     esp_err_to_name(ret));
            esp_codec_dev_delete(s_spk);
            s_spk = NULL;
        } else {
            ret = esp_codec_dev_set_out_vol(s_spk, AUDIO_DEFAULT_OUTPUT_VOLUME);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "speaker volume set failed: %s", esp_err_to_name(ret));
            }
            ret = esp_codec_dev_set_out_mute(s_spk, true);
            bool muted = (ret == ESP_OK);
            if (!muted) {
                ESP_LOGE(TAG, "speaker mute failed: %s", esp_err_to_name(ret));
            }
            portENTER_CRITICAL(&s_state_lock);
            s_playback_ready = true;
            s_output_muted = muted;
            s_tx_frames = 0;
            s_tx_underruns = 0;
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGI(TAG, "ES8311 playback ready, volume %d%%, %s",
                     AUDIO_DEFAULT_OUTPUT_VOLUME, muted ? "muted" : "UNMUTED");
        }
    }

    BaseType_t ok = xTaskCreatePinnedToCore(audio_rx_task, "audio_rx", 4096, NULL, 12, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "audio_rx task create failed");
        portENTER_CRITICAL(&s_state_lock);
        s_service_error = true;
        s_service_ready = false;
        s_playback_ready = false;
        portEXIT_CRITICAL(&s_state_lock);
        audio_service_delete_codecs();
        audio_service_delete_locks();
        return ESP_ERR_NO_MEM;
    }

    if (s_playback_ready) {
        ok = xTaskCreatePinnedToCore(audio_tx_task, "audio_tx", 4096, NULL, 11, NULL, 0);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "audio_tx task create failed; playback disabled");
            portENTER_CRITICAL(&s_state_lock);
            s_playback_ready = false;
            s_output_muted = true;
            portEXIT_CRITICAL(&s_state_lock);
            if (s_spk != NULL) {
                esp_codec_dev_delete(s_spk);
                s_spk = NULL;
            }
        }
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
    out->playback_ready = s_playback_ready;
    out->output_muted = s_output_muted;
    out->tx_frames = s_tx_frames;
    out->tx_underruns = s_tx_underruns;
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

esp_err_t audio_service_playback_open(const audio_playback_config_t *cfg,
                                      audio_playback_source_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    if (!s_started || s_playback_lock == NULL || !s_playback_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cfg == NULL || cfg->channels < 1 || cfg->channels > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    int priority = (int)cfg->priority;
    if (priority < 0 || priority >= AUDIO_PLAYBACK_LANES) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t ring_ms = cfg->ring_ms > 0 ? cfg->ring_ms : AUDIO_PLAYBACK_DEFAULT_RING_MS;
    uint64_t capacity64 = ((uint64_t)AUDIO_SAMPLE_RATE_HZ * ring_ms) / 1000;
    if (capacity64 == 0 || capacity64 > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    audio_ring_t *ring = audio_ring_create((uint32_t)capacity64);
    if (ring == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_playback_lock, portMAX_DELAY) != pdTRUE) {
        audio_ring_destroy(ring);
        return ESP_ERR_TIMEOUT;
    }

    struct audio_playback_source *src = &s_playback[priority];
    if (src->in_use) {
        xSemaphoreGive(s_playback_lock);
        audio_ring_destroy(ring);
        return ESP_ERR_INVALID_STATE;
    }

    src->ring = ring;
    src->priority = cfg->priority;
    src->channels = cfg->channels;
    src->trim_gain = powf(10.0f, cfg->gain_db / 20.0f);
    src->active = false;
    src->in_use = true;
    xSemaphoreGive(s_playback_lock);

    ESP_LOGI(TAG, "playback lane '%s' open: prio %d, %" PRIu32 " ms, %u ch, trim %.1f dB",
             cfg->name != NULL ? cfg->name : "unnamed", priority, ring_ms,
             (unsigned)cfg->channels, cfg->gain_db);
    *out = src;
    return ESP_OK;
}

esp_err_t audio_service_playback_write(audio_playback_source_t src,
                                       const int16_t *pcm,
                                       size_t frames,
                                       uint32_t timeout_ms,
                                       size_t *frames_written)
{
    if (src == NULL || pcm == NULL || frames_written == NULL || frames == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *frames_written = 0;

    if (!playback_source_in_pool(src) || s_playback_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_playback_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!src->in_use || src->ring == NULL) {
        xSemaphoreGive(s_playback_lock);
        return ESP_ERR_INVALID_STATE;
    }
    audio_ring_t *ring = src->ring;
    uint8_t channels = src->channels;
    xSemaphoreGive(s_playback_lock);

    TickType_t start = xTaskGetTickCount();
    TickType_t budget = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0 && budget == 0) {
        budget = 1;
    }

    size_t done = 0;
    while (done < frames) {
        size_t chunk = frames - done;
        if (chunk > AUDIO_FRAME_SAMPLES) {
            chunk = AUDIO_FRAME_SAMPLES;
        }

        while (audio_ring_free(ring) < chunk) {
            if (timeout_ms == 0) {
                return ESP_ERR_TIMEOUT;
            }
            TickType_t elapsed = xTaskGetTickCount() - start;
            if (elapsed >= budget) {
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(1);
        }

        int16_t mono_buf[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
        const int16_t *chunk_pcm;
        if (channels == 1) {
            audio_mixer_expand_mono(pcm + done, chunk, mono_buf);
            chunk_pcm = mono_buf;
        } else {
            chunk_pcm = pcm + done * AUDIO_CHANNELS;
        }

        size_t written = audio_ring_write(ring, chunk_pcm, chunk);
        if (written == 0) {
            continue;
        }
        done += written;
        *frames_written = done;
    }

    return ESP_OK;
}

esp_err_t audio_service_playback_set_active(audio_playback_source_t src, bool active)
{
    if (!playback_source_in_pool(src) || s_playback_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_playback_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    bool ok = src->in_use;
    if (ok) {
        src->active = active;
    }
    xSemaphoreGive(s_playback_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t audio_service_playback_flush(audio_playback_source_t src)
{
    if (!playback_source_in_pool(src) || s_playback_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_playback_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    bool ok = src->in_use && src->ring != NULL;
    if (ok) {
        audio_ring_reset(src->ring);
    }
    xSemaphoreGive(s_playback_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t audio_service_playback_close(audio_playback_source_t src)
{
    if (!playback_source_in_pool(src) || s_playback_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_playback_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!src->in_use) {
        xSemaphoreGive(s_playback_lock);
        return ESP_ERR_INVALID_STATE;
    }

    audio_ring_t *ring = src->ring;
    src->ring = NULL;
    src->active = false;
    src->in_use = false;
    xSemaphoreGive(s_playback_lock);

    audio_ring_destroy(ring);
    return ESP_OK;
}

esp_err_t audio_service_set_output_volume(int percent)
{
    if (percent < 0 || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_playback_ready || s_spk == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int ret = esp_codec_dev_set_out_vol(s_spk, percent);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "speaker volume set failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_state_lock);
    s_output_volume = percent;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

esp_err_t audio_service_get_output_volume(int *percent)
{
    if (percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_playback_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_state_lock);
    *percent = s_output_volume;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

esp_err_t audio_service_set_output_muted(bool muted)
{
    if (!s_playback_ready || s_spk == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int ret = esp_codec_dev_set_out_mute(s_spk, muted);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "speaker mute set failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_output_muted = muted;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}
