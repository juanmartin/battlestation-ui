#include "tuner_service.h"

#include <math.h>
#include <string.h>

#include "audio_format.h"
#include "audio_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings.h"

static const char *TAG = "tuner_service";

#define TUNER_SERVICE_TUNER_CORE 1
#define TUNER_SERVICE_TUNER_PRIO 5
#define TUNER_SERVICE_TUNER_STACK 6144

#define TUNER_SERVICE_GEN_CORE 0
#define TUNER_SERVICE_GEN_PRIO 10
#define TUNER_SERVICE_GEN_STACK 4096

#define TUNER_SERVICE_BLOCK_MS 10
#define TUNER_SERVICE_BLOCK_FRAMES AUDIO_FRAME_SAMPLES
#define TUNER_SERVICE_CAPTURE_RING_MS 200
#define TUNER_SERVICE_PLAYBACK_RING_MS 200
#define TUNER_SERVICE_READ_FRAMES (AUDIO_FRAME_SAMPLES * 2)
#define TUNER_SERVICE_WAVE_SAMPLES 256
#define TUNER_SERVICE_TONE_OFF_BLOCKS 5
#define TUNER_SERVICE_STOP_TIMEOUT_MS 2000
#define TUNER_SERVICE_TONE_DEFAULT_HZ 440.0f
#define TUNER_SERVICE_TONE_DEFAULT_LEVEL 0.30f

static bool s_started;
static bool s_broken;
static volatile bool s_running;
static tuner_engine_t *s_tuner;
static tone_engine_t s_tone;
static metronome_t s_metro;

static audio_capture_reader_t s_capture;
static audio_playback_source_t s_tone_src;
static audio_playback_source_t s_metro_src;

static TaskHandle_t s_tuner_task;
static TaskHandle_t s_gen_task;
static SemaphoreHandle_t s_tuner_exit;
static SemaphoreHandle_t s_gen_exit;
static SemaphoreHandle_t s_audio_lock;

static volatile bool s_tone_requested;
static volatile bool s_metro_requested;
static bool s_tone_lane_active;
static bool s_metro_lane_active;
static int s_tone_off_blocks;
static float s_tone_level = TUNER_SERVICE_TONE_DEFAULT_LEVEL;

static int16_t s_tone_buffer[TUNER_SERVICE_BLOCK_FRAMES * AUDIO_CHANNELS];
static int16_t s_metro_buffer[TUNER_SERVICE_BLOCK_FRAMES * AUDIO_CHANNELS];
static int16_t s_stereo_buffer[TUNER_SERVICE_READ_FRAMES * AUDIO_CHANNELS];
static int16_t s_mono_buffer[TUNER_SERVICE_READ_FRAMES];

static portMUX_TYPE s_wave_lock = portMUX_INITIALIZER_UNLOCKED;
static int16_t s_wave[TUNER_SERVICE_WAVE_SAMPLES];
static size_t s_wave_pos;

static void *tuner_alloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void wave_push(const int16_t *samples, size_t frames)
{
    portENTER_CRITICAL(&s_wave_lock);
    for (size_t i = 0; i < frames; i++) {
        s_wave[s_wave_pos] = samples[i];
        s_wave_pos = (s_wave_pos + 1u) % TUNER_SERVICE_WAVE_SAMPLES;
    }
    portEXIT_CRITICAL(&s_wave_lock);
}

static void tuner_task(void *arg)
{
    (void)arg;

    TickType_t last_pitch_log = 0;

    while (s_running) {
        size_t frames = 0;
        esp_err_t err = audio_service_capture_read(
            s_capture, s_stereo_buffer, TUNER_SERVICE_READ_FRAMES, 100, &frames);
        if (err == ESP_OK && frames > 0) {
            for (size_t i = 0; i < frames; i++) {
                int32_t left = s_stereo_buffer[i * AUDIO_CHANNELS + 0];
                int32_t right = s_stereo_buffer[i * AUDIO_CHANNELS + 1];
                s_mono_buffer[i] = (int16_t)((left + right) / 2);
            }
            wave_push(s_mono_buffer, frames);
            tuner_engine_process_block(s_tuner, s_mono_buffer, frames);

            TickType_t now = xTaskGetTickCount();
            if (now - last_pitch_log >= pdMS_TO_TICKS(1000)) {
                tuner_result_t result;
                if (tuner_engine_get_result(s_tuner, &result) &&
                    result.valid) {
                    ESP_LOGI(TAG, "pitch %.1f Hz %s%d %+.1f cents (%.0f%%)",
                             result.frequency_hz, result.note_name,
                             (int)result.octave, result.cents,
                             result.confidence * 100.0f);
                    last_pitch_log = now;
                }
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            break;
        }
    }

    if (s_tuner_exit != NULL) {
        xSemaphoreGive(s_tuner_exit);
    }
    vTaskDelete(NULL);
}

static void generator_task(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(TUNER_SERVICE_BLOCK_MS);

    while (s_running) {
        bool tone_on = false;
        bool metro_on = false;

        xSemaphoreTake(s_audio_lock, portMAX_DELAY);
        tone_on = s_tone_requested;
        metro_on = s_metro_requested;

        if (tone_on && !s_tone_lane_active) {
            tone_engine_reset(&s_tone);
            tone_engine_set_amplitude(&s_tone, s_tone_level);
            audio_service_playback_flush(s_tone_src);
            audio_service_playback_set_active(s_tone_src, true);
            s_tone_lane_active = true;
            s_tone_off_blocks = 0;
        } else if (tone_on && s_tone_lane_active) {
            if (s_tone_off_blocks > 0) {
                tone_engine_set_amplitude(&s_tone, s_tone_level);
                s_tone_off_blocks = 0;
            }
        } else if (!tone_on && s_tone_lane_active && s_tone_off_blocks == 0) {
            tone_engine_set_amplitude(&s_tone, 0.0f);
            s_tone_off_blocks = TUNER_SERVICE_TONE_OFF_BLOCKS;
        }

        if (metro_on && !s_metro_lane_active) {
            metronome_reset(&s_metro);
            audio_service_playback_flush(s_metro_src);
            audio_service_playback_set_active(s_metro_src, true);
            s_metro_lane_active = true;
        } else if (!metro_on && s_metro_lane_active) {
            metronome_reset(&s_metro);
            audio_service_playback_set_active(s_metro_src, false);
            audio_service_playback_flush(s_metro_src);
            s_metro_lane_active = false;
        }

        if (s_tone_lane_active) {
            tone_engine_render(&s_tone, s_tone_buffer,
                               TUNER_SERVICE_BLOCK_FRAMES);
        }
        if (s_metro_lane_active) {
            metronome_render(&s_metro, s_metro_buffer,
                             TUNER_SERVICE_BLOCK_FRAMES);
        }
        xSemaphoreGive(s_audio_lock);

        if (s_tone_lane_active) {
            size_t written = 0;
            esp_err_t err = audio_service_playback_write(
                s_tone_src, s_tone_buffer, TUNER_SERVICE_BLOCK_FRAMES, 10,
                &written);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "tone write: %s (%u/%u)", esp_err_to_name(err),
                         (unsigned)written,
                         (unsigned)TUNER_SERVICE_BLOCK_FRAMES);
            }
        }
        if (s_metro_lane_active) {
            size_t written = 0;
            esp_err_t err = audio_service_playback_write(
                s_metro_src, s_metro_buffer, TUNER_SERVICE_BLOCK_FRAMES, 10,
                &written);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "metronome write: %s (%u/%u)",
                         esp_err_to_name(err), (unsigned)written,
                         (unsigned)TUNER_SERVICE_BLOCK_FRAMES);
            }
        }

        if (!tone_on && s_tone_lane_active && s_tone_off_blocks > 0) {
            s_tone_off_blocks--;
            if (s_tone_off_blocks == 0) {
                audio_service_playback_set_active(s_tone_src, false);
                audio_service_playback_flush(s_tone_src);
                s_tone_lane_active = false;
            }
        }

        vTaskDelayUntil(&last_wake, period);
        if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            break;
        }
    }

    if (s_gen_exit != NULL) {
        xSemaphoreGive(s_gen_exit);
    }
    vTaskDelete(NULL);
}

static void close_resources(void)
{
    if (s_capture != NULL) {
        audio_service_capture_close(s_capture);
        s_capture = NULL;
    }
    if (s_tone_src != NULL) {
        audio_service_playback_set_active(s_tone_src, false);
        audio_service_playback_close(s_tone_src);
        s_tone_src = NULL;
    }
    if (s_metro_src != NULL) {
        audio_service_playback_set_active(s_metro_src, false);
        audio_service_playback_close(s_metro_src);
        s_metro_src = NULL;
    }
    if (s_tuner != NULL) {
        tuner_engine_destroy(s_tuner);
        s_tuner = NULL;
    }
    if (s_audio_lock != NULL) {
        vSemaphoreDelete(s_audio_lock);
        s_audio_lock = NULL;
    }
    if (s_tuner_exit != NULL) {
        vSemaphoreDelete(s_tuner_exit);
        s_tuner_exit = NULL;
    }
    if (s_gen_exit != NULL) {
        vSemaphoreDelete(s_gen_exit);
        s_gen_exit = NULL;
    }
    audio_service_set_output_muted(true);
}

esp_err_t tuner_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (s_broken) {
        return ESP_ERR_INVALID_STATE;
    }

    s_audio_lock = xSemaphoreCreateMutex();
    s_tuner_exit = xSemaphoreCreateBinary();
    s_gen_exit = xSemaphoreCreateBinary();
    if (s_audio_lock == NULL || s_tuner_exit == NULL || s_gen_exit == NULL) {
        close_resources();
        return ESP_ERR_NO_MEM;
    }

    tuner_engine_config_t tuner_cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .window_size = 2048,
        .hop_size = 800,
        /* Reject the room's low-frequency rumble; an instrument is well above
         * this. Keeps the needle on "no signal" instead of inventing notes. */
        .min_rms = 0.003f,
        .alloc = tuner_alloc,
        .free = heap_caps_free,
    };
    s_tuner = tuner_engine_create(&tuner_cfg);
    if (s_tuner == NULL) {
        ESP_LOGE(TAG, "tuner engine create failed");
        close_resources();
        return ESP_ERR_NO_MEM;
    }

    const app_settings_t *settings = settings_get();
    if (settings != NULL) {
        tuner_engine_set_a4_reference(s_tuner, settings->a4_hz);
    }

    tone_engine_config_t tone_cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .waveform = TONE_WAVE_SINE,
        .frequency_hz = TUNER_SERVICE_TONE_DEFAULT_HZ,
        .amplitude = TUNER_SERVICE_TONE_DEFAULT_LEVEL,
        .ramp_ms = 5.0f,
    };
    if (!tone_engine_init(&s_tone, &tone_cfg)) {
        ESP_LOGE(TAG, "tone engine init failed");
        close_resources();
        return ESP_ERR_INVALID_STATE;
    }

    metronome_config_t metro_cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .bpm = 120.0f,
        .meter = METRONOME_METER_4_4,
        .accent_beat = 1,
        .click_wave = METRONOME_CLICK_SINE,
        /* Zero gains are valid engine values, so spell out the defaults
         * here instead of relying on the engine's fallback. */
        .accent_frequency_hz = METRONOME_DEFAULT_ACCENT_HZ,
        .beat_frequency_hz = METRONOME_DEFAULT_BEAT_HZ,
        .accent_gain = METRONOME_DEFAULT_ACCENT_GAIN,
        .beat_gain = METRONOME_DEFAULT_BEAT_GAIN,
        .click_ms = METRONOME_DEFAULT_CLICK_MS,
        .attack_ms = METRONOME_DEFAULT_ATTACK_MS,
    };
    if (!metronome_init(&s_metro, &metro_cfg)) {
        ESP_LOGE(TAG, "metronome init failed");
        close_resources();
        return ESP_ERR_INVALID_STATE;
    }

    audio_capture_config_t capture_cfg = {
        .name = "tuner",
        .ring_ms = TUNER_SERVICE_CAPTURE_RING_MS,
    };
    esp_err_t err = audio_service_capture_open(&capture_cfg, &s_capture);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "capture open failed: %s", esp_err_to_name(err));
        close_resources();
        return err;
    }

    audio_playback_config_t tone_out = {
        .name = "tone",
        .priority = AUDIO_PRIO_TONE,
        .ring_ms = TUNER_SERVICE_PLAYBACK_RING_MS,
        .channels = AUDIO_CHANNELS,
        .gain_db = 0.0f,
    };
    err = audio_service_playback_open(&tone_out, &s_tone_src);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tone lane open failed: %s", esp_err_to_name(err));
        close_resources();
        return err;
    }

    audio_playback_config_t metro_out = {
        .name = "metronome",
        .priority = AUDIO_PRIO_METRONOME,
        .ring_ms = TUNER_SERVICE_PLAYBACK_RING_MS,
        .channels = AUDIO_CHANNELS,
        .gain_db = 0.0f,
    };
    err = audio_service_playback_open(&metro_out, &s_metro_src);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "metronome lane open failed: %s", esp_err_to_name(err));
        close_resources();
        return err;
    }

    err = audio_service_set_output_muted(false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "output unmute failed: %s", esp_err_to_name(err));
    }

    s_tone_requested = false;
    s_metro_requested = false;
    s_tone_lane_active = false;
    s_metro_lane_active = false;
    s_tone_off_blocks = 0;
    s_wave_pos = 0;
    memset(s_wave, 0, sizeof(s_wave));
    s_running = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        tuner_task, "tuner_dsp", TUNER_SERVICE_TUNER_STACK, NULL,
        TUNER_SERVICE_TUNER_PRIO, &s_tuner_task, TUNER_SERVICE_TUNER_CORE);
    if (ok != pdPASS) {
        s_running = false;
        close_resources();
        return ESP_ERR_NO_MEM;
    }

    ok = xTaskCreatePinnedToCore(
        generator_task, "tuner_gen", TUNER_SERVICE_GEN_STACK, NULL,
        TUNER_SERVICE_GEN_PRIO, &s_gen_task, TUNER_SERVICE_GEN_CORE);
    if (ok != pdPASS) {
        s_running = false;
        xTaskNotifyGive(s_tuner_task);
        xSemaphoreTake(s_tuner_exit, pdMS_TO_TICKS(TUNER_SERVICE_STOP_TIMEOUT_MS));
        s_tuner_task = NULL;
        close_resources();
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "started: 16 kHz tuner + tone/metronome lanes");
    return ESP_OK;
}

void tuner_service_stop(void)
{
    if (!s_started) {
        return;
    }

    ESP_LOGI(TAG, "stop");
    audio_service_set_output_muted(true);
    s_running = false;
    if (s_tuner_task != NULL) {
        xTaskNotifyGive(s_tuner_task);
    }
    if (s_gen_task != NULL) {
        xTaskNotifyGive(s_gen_task);
    }

    bool tuner_stopped = true;
    bool gen_stopped = true;
    if (s_tuner_task != NULL) {
        tuner_stopped =
            xSemaphoreTake(s_tuner_exit,
                           pdMS_TO_TICKS(TUNER_SERVICE_STOP_TIMEOUT_MS)) == pdTRUE;
    }
    if (s_gen_task != NULL) {
        gen_stopped =
            xSemaphoreTake(s_gen_exit,
                           pdMS_TO_TICKS(TUNER_SERVICE_STOP_TIMEOUT_MS)) == pdTRUE;
    }
    s_tuner_task = NULL;
    s_gen_task = NULL;

    if (!tuner_stopped || !gen_stopped) {
        ESP_LOGE(TAG, "task stop timeout (tuner=%d gen=%d); leaking resources",
                 tuner_stopped, gen_stopped);
        /* Leave every handle/pointer intact: the still-running task keeps
         * using them. Do not allow a second start against the same state. */
        s_broken = true;
        s_started = false;
        return;
    }

    close_resources();
    s_started = false;
    ESP_LOGI(TAG, "stopped");
}

bool tuner_service_running(void)
{
    return s_started;
}

bool tuner_service_get_result(tuner_result_t *out)
{
    if (!s_started || s_tuner == NULL || out == NULL) {
        return false;
    }
    return tuner_engine_get_result(s_tuner, out);
}

void tuner_service_set_a4_reference(float a4_hz)
{
    if (s_started && s_tuner != NULL) {
        tuner_engine_set_a4_reference(s_tuner, a4_hz);
    }
}

int tuner_service_get_output_volume(void)
{
    int percent = 60;
    if (audio_service_get_output_volume(&percent) != ESP_OK) {
        return 60;
    }
    return percent;
}

esp_err_t tuner_service_set_output_volume(int percent)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    return audio_service_set_output_volume(percent);
}

void tuner_service_get_waveform(int16_t *out, size_t max_samples,
                                size_t *samples)
{
    if (out == NULL || samples == NULL) {
        return;
    }
    *samples = 0;
    if (!s_started) {
        return;
    }

    size_t count = max_samples;
    if (count > TUNER_SERVICE_WAVE_SAMPLES) {
        count = TUNER_SERVICE_WAVE_SAMPLES;
    }
    if (count == 0) {
        return;
    }

    portENTER_CRITICAL(&s_wave_lock);
    size_t start = (s_wave_pos + TUNER_SERVICE_WAVE_SAMPLES - count) %
                   TUNER_SERVICE_WAVE_SAMPLES;
    for (size_t i = 0; i < count; i++) {
        out[i] = s_wave[(start + i) % TUNER_SERVICE_WAVE_SAMPLES];
    }
    portEXIT_CRITICAL(&s_wave_lock);
    *samples = count;
}

static bool take_audio_lock(void)
{
    return s_started && s_audio_lock != NULL &&
           xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(50)) == pdTRUE;
}

static void give_audio_lock(void)
{
    xSemaphoreGive(s_audio_lock);
}

esp_err_t tuner_service_set_tone_enabled(bool enabled)
{
    if (!s_started || !take_audio_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_tone_requested = enabled;
    give_audio_lock();
    ESP_LOGI(TAG, "tone %s", enabled ? "on" : "off");
    return ESP_OK;
}

bool tuner_service_tone_enabled(void)
{
    return s_tone_requested;
}

void tuner_service_set_tone_waveform(tone_wave_t waveform)
{
    if (take_audio_lock()) {
        tone_engine_set_waveform(&s_tone, waveform);
        give_audio_lock();
    }
}

void tuner_service_set_tone_frequency(float frequency_hz)
{
    if (take_audio_lock()) {
        tone_engine_set_frequency(&s_tone, frequency_hz);
        give_audio_lock();
    }
}

void tuner_service_set_tone_amplitude(float amplitude)
{
    if (!take_audio_lock()) {
        return;
    }
    s_tone_level = isfinite(amplitude) ? amplitude : 0.0f;
    if (s_tone_level < 0.0f) {
        s_tone_level = 0.0f;
    } else if (s_tone_level > 1.0f) {
        s_tone_level = 1.0f;
    }
    tone_engine_set_amplitude(&s_tone, s_tone_level);
    give_audio_lock();
}

esp_err_t tuner_service_start_sweep(tone_sweep_mode_t mode, float start_hz,
                                    float end_hz, float duration_s)
{
    if (!s_started || !take_audio_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = tone_engine_start_sweep(&s_tone, mode, start_hz, end_hz,
                                      duration_s);
    give_audio_lock();
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void tuner_service_stop_sweep(void)
{
    if (take_audio_lock()) {
        tone_engine_stop_sweep(&s_tone);
        give_audio_lock();
    }
}

esp_err_t tuner_service_set_metronome_enabled(bool enabled)
{
    if (!s_started || !take_audio_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_metro_requested = enabled;
    give_audio_lock();
    ESP_LOGI(TAG, "metronome %s", enabled ? "on" : "off");
    return ESP_OK;
}

bool tuner_service_metronome_enabled(void)
{
    return s_metro_requested;
}

void tuner_service_set_metronome_bpm(float bpm)
{
    if (take_audio_lock()) {
        metronome_set_bpm(&s_metro, bpm);
        give_audio_lock();
    }
}

void tuner_service_set_metronome_meter(metronome_meter_t meter)
{
    if (take_audio_lock()) {
        metronome_set_meter(&s_metro, meter);
        give_audio_lock();
    }
}

void tuner_service_set_metronome_accent(uint8_t beat)
{
    if (take_audio_lock()) {
        metronome_set_accent_beat(&s_metro, beat);
        give_audio_lock();
    }
}

void tuner_service_set_metronome_click(metronome_click_wave_t wave)
{
    if (take_audio_lock()) {
        metronome_set_click_wave(&s_metro, wave);
        give_audio_lock();
    }
}

void tuner_service_get_metronome_state(uint32_t *beat_count,
                                       uint32_t *bar_count,
                                       uint8_t *beat_in_bar)
{
    uint32_t beats = 0;
    uint32_t bars = 0;
    uint8_t in_bar = 0;

    if (take_audio_lock()) {
        beats = metronome_beat_count(&s_metro);
        bars = metronome_bar_count(&s_metro);
        in_bar = metronome_beat_in_bar(&s_metro);
        give_audio_lock();
    }

    if (beat_count != NULL) {
        *beat_count = beats;
    }
    if (bar_count != NULL) {
        *bar_count = bars;
    }
    if (beat_in_bar != NULL) {
        *beat_in_bar = in_bar;
    }
}
