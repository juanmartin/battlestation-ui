#include "diag_probe.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_probe.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ui_probe.h"

static const char *TAG = "diag_probe";
static lv_display_t *s_disp;

static const char *task_state_name(eTaskState state)
{
    switch (state) {
    case eRunning:
        return "running";
    case eReady:
        return "ready";
    case eBlocked:
        return "blocked";
    case eSuspended:
        return "suspended";
    case eDeleted:
        return "deleted";
    default:
        return "invalid";
    }
}

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    default:
        return "OTHER";
    }
}

static void diag_task(void *arg)
{
    (void)arg;

    temperature_sensor_handle_t tsens = NULL;
    temperature_sensor_config_t tsens_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tsens_cfg, &tsens) == ESP_OK) {
        if (temperature_sensor_enable(tsens) != ESP_OK) {
            temperature_sensor_uninstall(tsens);
            tsens = NULL;
        }
    }

    ESP_LOGI(TAG, "reset reason: %s; heartbeat starting", reset_reason_name(esp_reset_reason()));

    uint32_t last_heartbeat = 0;
    uint32_t stall_cycles = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        uint32_t heartbeat = ui_probe_heartbeat();
        uint32_t fps = ui_probe_fps();
        TaskHandle_t lvgl = xTaskGetHandle("lvgl");
        TaskHandle_t audio = xTaskGetHandle("audio_probe");
        eTaskState lvgl_state = lvgl ? eTaskGetState(lvgl) : eDeleted;
        eTaskState audio_state = audio ? eTaskGetState(audio) : eDeleted;
        UBaseType_t lvgl_stack = lvgl ? uxTaskGetStackHighWaterMark(lvgl) : 0;
        UBaseType_t audio_stack = audio ? uxTaskGetStackHighWaterMark(audio) : 0;
        float temp_c = 0.0f;
        if (tsens != NULL && temperature_sensor_get_celsius(tsens, &temp_c) != ESP_OK) {
            temp_c = -999.0f;
        }

        bool stalled = (heartbeat == last_heartbeat);
        if (stalled) {
            stall_cycles++;
        } else {
            stall_cycles = 0;
        }
        last_heartbeat = heartbeat;

        ESP_LOGI(TAG,
                 "heartbeat=%u fps=%u lvgl=%s/%u audio=%s/%u int=%u psram=%u temp=%.1f stall=%u",
                 (unsigned)heartbeat, (unsigned)fps,
                 task_state_name(lvgl_state), (unsigned)(lvgl_stack * sizeof(StackType_t)),
                 task_state_name(audio_state), (unsigned)(audio_stack * sizeof(StackType_t)),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 temp_c, (unsigned)stall_cycles);

        if (stall_cycles >= 2) {
            ESP_LOGW(TAG,
                     "UI STALL: heartbeat has not moved for %u s (lvgl=%s, fps=%u)",
                     (unsigned)(stall_cycles * 5), task_state_name(lvgl_state), (unsigned)fps);
        }
    }
}

esp_err_t diag_probe_start(lv_display_t *disp)
{
    if (disp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_disp = disp;
    BaseType_t ok = xTaskCreatePinnedToCore(diag_task, "diag_probe", 4096, NULL, 2, NULL, 0);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
