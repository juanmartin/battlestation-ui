#include "apps.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "audio_analysis.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "organism_engine.h"
#include "sdkconfig.h"
#include "ui_theme.h"

static const char *TAG = "organism";

#define ORGANISM_WIDTH 1024
#define ORGANISM_HEIGHT 600
#define ORGANISM_PARTICLES 3072
#define ORGANISM_TASK_CORE 1
#define ORGANISM_TASK_PRIORITY 4
#define ORGANISM_TASK_STACK 8192
#define ORGANISM_FRAME_MS 33
#define ORGANISM_LOG_INTERVAL_US 5000000
#define ORGANISM_TASK_STOP_TIMEOUT_MS 2000

#define ORGANISM_BUFFER_BYTES \
    ((size_t)ORGANISM_WIDTH * (size_t)ORGANISM_HEIGHT * sizeof(uint16_t))

#define ORGANISM_DB_MIN (-80.0f)
#define ORGANISM_DB_MAX (-20.0f)

#if CONFIG_APP_ROTATION_180
#define ORGANISM_ROTATE_180 true
#else
#define ORGANISM_ROTATE_180 false
#endif

/* Matches the app_manager HOME button and its touch hotspot. */
#define ORGANISM_HOME_X 914
#define ORGANISM_HOME_Y 16
#define ORGANISM_HOME_W 78
#define ORGANISM_HOME_H 36
#define ORGANISM_HOME_HOTSPOT_X 900
#define ORGANISM_HOME_HOTSPOT_Y 60

#define HINT_SCALE 2
#define HINT_GLYPH_W 5
#define HINT_GLYPH_H 7
#define HINT_GAP 2
#define HINT_TEXT_W (4 * HINT_GLYPH_W * HINT_SCALE + 3 * HINT_GAP)
#define HINT_TEXT_H (HINT_GLYPH_H * HINT_SCALE)

#define COLOR_HINT_BG 0x0862u     /* 0x0B0F14 in RGB565 */
#define COLOR_HINT_BORDER 0x269Du /* 0x22D3EE in RGB565 */
#define COLOR_HINT_TEXT 0xFFFFu

_Static_assert(AUDIO_BAND_COUNT == 16u, "organism maps all 16 audio bands");
_Static_assert(ORGANISM_WIDTH % 2 == 0, "rotated copy handles pixel pairs");

typedef struct {
    int16_t x;
    int16_t y;
    bool active;
} organism_touch_state_t;

static uint16_t *s_last;
static lv_obj_t *s_overlay;
static lv_display_t *s_disp;
static bool s_dummy_enabled;
static organism_engine_t *s_engine;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_task_exit;

static portMUX_TYPE s_touch_lock = portMUX_INITIALIZER_UNLOCKED;
static organism_touch_state_t s_touch;

static const uint8_t s_home_font[4][HINT_GLYPH_H] = {
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* H */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
};

static void *organism_alloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void organism_free(void *ptr)
{
    heap_caps_free(ptr);
}

static float db_to_unit(float db)
{
    if (!isfinite(db)) {
        return 0.0f;
    }
    float unit = (db - ORGANISM_DB_MIN) / (ORGANISM_DB_MAX - ORGANISM_DB_MIN);
    if (unit <= 0.0f) {
        return 0.0f;
    }
    if (unit >= 1.0f) {
        return 1.0f;
    }
    return unit;
}

static float band_unit(const audio_features_t *features, int first, int last)
{
    float sum = 0.0f;
    for (int i = first; i <= last; i++) {
        sum += features->band_db[i];
    }
    return db_to_unit(sum / (float)(last - first + 1));
}

static void touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    bool active = (code == LV_EVENT_PRESSED) || (code == LV_EVENT_PRESSING);
    if (indev == NULL && active) {
        return;
    }

    lv_point_t point = { 0, 0 };
    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
    }

    if (indev != NULL &&
        point.x > ORGANISM_HOME_HOTSPOT_X &&
        point.y < ORGANISM_HOME_HOTSPOT_Y) {
        if (code == LV_EVENT_PRESSED) {
            app_manager_request("launcher");
        }
        portENTER_CRITICAL(&s_touch_lock);
        s_touch.active = false;
        portEXIT_CRITICAL(&s_touch_lock);
        return;
    }

    portENTER_CRITICAL(&s_touch_lock);
    if (indev != NULL) {
        s_touch.x = (int16_t)point.x;
        s_touch.y = (int16_t)point.y;
    }
    s_touch.active = active;
    portEXIT_CRITICAL(&s_touch_lock);
}

/*
 * The adapter normally rotates LVGL's logical output into the panel
 * framebuffer. Dummy draw bypasses that step, so the application must write
 * the physical buffer itself. The panel runs at 180 degrees on this board.
 */
static inline void hint_put(uint16_t *dst, int x, int y, uint16_t color)
{
#if CONFIG_APP_ROTATION_180
    dst[(size_t)(ORGANISM_HEIGHT - 1 - y) * ORGANISM_WIDTH +
        (size_t)(ORGANISM_WIDTH - 1 - x)] = color;
#else
    dst[(size_t)y * ORGANISM_WIDTH + (size_t)x] = color;
#endif
}

static void hint_fill(uint16_t *dst, int x, int y, int w, int h, uint16_t color)
{
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            hint_put(dst, x + col, y + row, color);
        }
    }
}

static void draw_home_hint(uint16_t *dst)
{
    hint_fill(dst, ORGANISM_HOME_X, ORGANISM_HOME_Y,
              ORGANISM_HOME_W, ORGANISM_HOME_H, COLOR_HINT_BORDER);
    hint_fill(dst, ORGANISM_HOME_X + 1, ORGANISM_HOME_Y + 1,
              ORGANISM_HOME_W - 2, ORGANISM_HOME_H - 2, COLOR_HINT_BG);

    const int text_x = ORGANISM_HOME_X + (ORGANISM_HOME_W - HINT_TEXT_W) / 2;
    const int text_y = ORGANISM_HOME_Y + (ORGANISM_HOME_H - HINT_TEXT_H) / 2;

    for (int glyph = 0; glyph < 4; glyph++) {
        const int glyph_x = text_x + glyph * (HINT_GLYPH_W * HINT_SCALE + HINT_GAP);
        for (int row = 0; row < HINT_GLYPH_H; row++) {
            uint8_t bits = s_home_font[glyph][row];
            for (int col = 0; col < HINT_GLYPH_W; col++) {
                if ((bits & (1u << (HINT_GLYPH_W - 1 - col))) == 0) {
                    continue;
                }
                hint_fill(dst, glyph_x + col * HINT_SCALE,
                          text_y + row * HINT_SCALE,
                          HINT_SCALE, HINT_SCALE, COLOR_HINT_TEXT);
            }
        }
    }
}

static void engine_task(void *arg)
{
    SemaphoreHandle_t exit_sem = (SemaphoreHandle_t)arg;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(ORGANISM_FRAME_MS);
    uint32_t frames = 0;
    uint32_t interval_frames = 0;
    uint32_t interval_errors = 0;
    uint64_t step_sum = 0;
    uint64_t copy_sum = 0;
    uint64_t flush_sum = 0;
    int64_t logged_us = esp_timer_get_time();

    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            break;
        }

        TickType_t frame_start_ticks = xTaskGetTickCount();

        audio_features_t features;
        if (audio_analysis_get_features(&features) == ESP_OK) {
            organism_audio_t audio = {
                .bass = band_unit(&features, 0, 3),
                .mid = band_unit(&features, 4, 9),
                .treble = band_unit(&features, 10, 15),
                .level = db_to_unit(features.levels.dbfs_avg),
            };
            organism_engine_set_audio(s_engine, &audio);
        }

        organism_touch_t touch;
        portENTER_CRITICAL(&s_touch_lock);
        touch.x = s_touch.x;
        touch.y = s_touch.y;
        touch.active = s_touch.active;
        portEXIT_CRITICAL(&s_touch_lock);
        organism_engine_set_touch(s_engine, &touch);

        int64_t step_start_us = esp_timer_get_time();
        void *dst = esp_lv_adapter_dummy_draw_get_free_buf(s_disp);
        int64_t step_done_us = step_start_us;
        int64_t copy_done_us = step_start_us;
        int64_t flush_done_us = step_start_us;

        if (dst != NULL && dst != s_last) {
            organism_engine_step_into(s_engine, (uint16_t *)dst, s_last);
            draw_home_hint((uint16_t *)dst);
            step_done_us = esp_timer_get_time();
            copy_done_us = step_done_us;

            esp_err_t err = esp_lv_adapter_dummy_draw_flush_buf(s_disp, dst);
            flush_done_us = esp_timer_get_time();
            if (err == ESP_OK) {
                s_last = (uint16_t *)dst;
            } else {
                interval_errors++;
            }
        } else {
            interval_errors++;
        }

        step_sum += (uint64_t)(step_done_us - step_start_us);
        copy_sum += (uint64_t)(copy_done_us - step_done_us);
        flush_sum += (uint64_t)(flush_done_us - copy_done_us);
        frames++;
        interval_frames++;

        int64_t now_us = esp_timer_get_time();
        int64_t elapsed_us = now_us - logged_us;
        if (elapsed_us >= ORGANISM_LOG_INTERVAL_US) {
            const float fps = (float)interval_frames * 1000000.0f /
                              (float)elapsed_us;
            const unsigned step_avg =
                (unsigned)(step_sum / (interval_frames ? interval_frames : 1u));
            const unsigned copy_avg =
                (unsigned)(copy_sum / (interval_frames ? interval_frames : 1u));
            const unsigned flush_avg =
                (unsigned)(flush_sum / (interval_frames ? interval_frames : 1u));

            ESP_LOGI(TAG,
                     "fps=%.1f step_us=%u copy_us=%u flush_us=%u "
                     "particles=%d frame=%u errors=%u",
                     fps, step_avg, copy_avg, flush_avg, ORGANISM_PARTICLES,
                     (unsigned)frames, (unsigned)interval_errors);

            logged_us = now_us;
            interval_frames = 0;
            interval_errors = 0;
            step_sum = 0;
            copy_sum = 0;
            flush_sum = 0;
        }

        /* vTaskDelayUntil returns immediately after an overrun. Reset the
         * cadence and give IDLE1 one tick in that case so the task watchdog
         * is fed even if a frame takes much longer than 33 ms. */
        if ((xTaskGetTickCount() - frame_start_ticks) >= period) {
            vTaskDelay(1);
            last_wake = xTaskGetTickCount();
        } else {
            vTaskDelayUntil(&last_wake, period);
        }
    }

    if (exit_sem != NULL) {
        xSemaphoreGive(exit_sem);
    }
    vTaskDelete(NULL);
}

static void free_canvas(void)
{
    if (s_engine != NULL) {
        organism_engine_destroy(s_engine);
        s_engine = NULL;
    }
    s_last = NULL;
    if (s_task_exit != NULL) {
        vSemaphoreDelete(s_task_exit);
        s_task_exit = NULL;
    }
}

static void release_ui_refs(void)
{
    /* app_manager deletes the overlay with the rest of the app tree. */
    s_overlay = NULL;
}

static bool stop_task(void)
{
    if (s_task == NULL) {
        return true;
    }

    xTaskNotifyGive(s_task);
    bool exited = s_task_exit != NULL &&
                  xSemaphoreTake(s_task_exit,
                                 pdMS_TO_TICKS(ORGANISM_TASK_STOP_TIMEOUT_MS)) == pdTRUE;
    s_task = NULL;
    if (!exited) {
        ESP_LOGE(TAG, "engine task did not stop; leaving canvas allocated");
    }
    return exited;
}

static void show_error(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = ui_theme_label(parent, text, &lv_font_montserrat_20,
                                     0xF87171, 60, 60);
    lv_obj_set_width(label, ORGANISM_WIDTH - 120);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

static esp_err_t mount_failed(lv_obj_t *parent, const char *text, esp_err_t err)
{
    release_ui_refs();

    if (s_dummy_enabled && s_disp != NULL) {
        esp_err_t disable_err = esp_lv_adapter_set_dummy_draw(s_disp, false);
        if (disable_err != ESP_OK) {
            ESP_LOGE(TAG, "dummy draw disable failed: %s",
                     esp_err_to_name(disable_err));
        }
        s_dummy_enabled = false;
    }

    free_canvas();
    ESP_LOGE(TAG, "%s", text);
    show_error(parent, text);
    return err;
}

esp_err_t organism_mount(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_disp = lv_display_get_default();
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "no display");
        show_error(parent, "Organism needs a display");
        return ESP_ERR_NOT_FOUND;
    }

    organism_engine_config_t cfg = {
        .width = ORGANISM_WIDTH,
        .height = ORGANISM_HEIGHT,
        .particle_count = ORGANISM_PARTICLES,
        .buffers = { NULL, NULL, NULL },
        .alloc = organism_alloc,
        .free = organism_free,
        .seed = (uint32_t)esp_timer_get_time(),
        .rotate_180 = ORGANISM_ROTATE_180,
    };
    s_engine = organism_engine_create(&cfg);
    if (s_engine == NULL) {
        return mount_failed(parent, "Organism engine could not start",
                            ESP_ERR_NO_MEM);
    }

    s_task_exit = xSemaphoreCreateBinary();
    if (s_task_exit == NULL) {
        return mount_failed(parent, "Organism engine could not start",
                            ESP_ERR_NO_MEM);
    }

    esp_err_t err = esp_lv_adapter_set_dummy_draw(s_disp, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dummy draw enable failed: %s", esp_err_to_name(err));
        /* Best effort: leave the display in a usable state for the launcher. */
        esp_err_t disable_err = esp_lv_adapter_set_dummy_draw(s_disp, false);
        if (disable_err != ESP_OK) {
            ESP_LOGE(TAG, "dummy draw rollback failed: %s",
                     esp_err_to_name(disable_err));
        }
        return mount_failed(parent, "Organism display mode failed", err);
    }
    s_dummy_enabled = true;

    s_overlay = lv_obj_create(parent);
    if (s_overlay == NULL) {
        return mount_failed(parent, "Organism overlay could not start",
                            ESP_ERR_NO_MEM);
    }
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, ORGANISM_WIDTH, ORGANISM_HEIGHT);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_add_event_cb(s_overlay, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_overlay, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_overlay, touch_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_overlay, touch_event_cb, LV_EVENT_PRESS_LOST, NULL);

    portENTER_CRITICAL(&s_touch_lock);
    s_touch.active = false;
    s_touch.x = 0;
    s_touch.y = 0;
    portEXIT_CRITICAL(&s_touch_lock);

    BaseType_t created = xTaskCreatePinnedToCore(engine_task, "organism",
                                                 ORGANISM_TASK_STACK, s_task_exit,
                                                 ORGANISM_TASK_PRIORITY, &s_task,
                                                 ORGANISM_TASK_CORE);
    if (created != pdPASS) {
        s_task = NULL;
        return mount_failed(parent, "Organism engine task could not start",
                            ESP_ERR_NO_MEM);
    }

    ESP_LOGI(TAG, "mounted: %dx%d dummy draw, %u KB per buffer, %d particles",
             ORGANISM_WIDTH, ORGANISM_HEIGHT,
             (unsigned)(ORGANISM_BUFFER_BYTES / 1024u), ORGANISM_PARTICLES);
    return ESP_OK;
}

void organism_unmount(void)
{
    release_ui_refs();

    bool stopped = stop_task();
    if (stopped && s_dummy_enabled && s_disp != NULL) {
        esp_err_t err = esp_lv_adapter_set_dummy_draw(s_disp, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "dummy draw disable failed: %s", esp_err_to_name(err));
        }
        s_dummy_enabled = false;
    }

    if (stopped) {
        free_canvas();
    } else {
        /* The task still owns the engine and the exit semaphore; leaking is
         * safer than a use-after-free. */
        s_engine = NULL;
        s_last = NULL;
        s_task_exit = NULL;
    }

    s_disp = NULL;
}

const app_desc_t organism_app = {
    .id = "organism",
    .title = "Organism",
    .description = "Generative visuals driven by sound.",
    .mount = organism_mount,
    .unmount = organism_unmount,
};
