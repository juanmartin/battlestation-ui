#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_analysis.h"
#include "audio_service.h"
#include "app_manager.h"
#include "diag_probe.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "ha_client.h"
#include "settings.h"
#include "temperature_service.h"
#include "time_service.h"
#include "wifi_probe.h"

#if CONFIG_APP_AUDIO_TEST_TONE
#include "audio_service_selftest.h"
#endif
#if CONFIG_APP_HA_TEST
#include "ha_client_selftest.h"
#endif

static const char *TAG = "battlestation";

void app_main(void)
{
    ESP_LOGI(TAG, "Battlestation bring-up starting");
    ESP_LOGI(TAG, "internal heap=%u  PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    esp_err_t settings_err = settings_init();
    if (settings_err != ESP_OK) {
        ESP_LOGW(TAG, "settings init failed: %s; using build defaults",
                 esp_err_to_name(settings_err));
    }
    const app_settings_t *settings = settings_get();

    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
#if CONFIG_APP_ROTATION_180
        .rotation = ESP_LV_ADAPTER_ROTATE_180,
#else
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
#endif
#if CONFIG_APP_TEAR_TRIPLE_FULL
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL,
#elif CONFIG_APP_TEAR_DOUBLE_DIRECT
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT,
#else
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
#endif
#if CONFIG_APP_ROTATION_180
        /* Empirical on this 7B unit: 180-degree display rotation needs
         * unmirrored GT911 coordinates. The BSP/09 default (180 + mirror x/y)
         * reports the visual top-left as (1023,599). */
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
#else
        /* 0-degree diagnostic orientation, verified with 08_lvgl_display_panel. */
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
#endif
    };

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "display start failed");
        abort();
    }
    bsp_display_brightness_set(settings != NULL ? settings->brightness
                                                : CONFIG_APP_BACKLIGHT_PERCENT);

    /* Audio first: the UI timer starts reading its snapshot immediately. */
    if (settings != NULL) {
        esp_err_t gain_err =
            audio_service_set_mic_gain((float)settings->mic_gain_db);
        if (gain_err != ESP_OK) {
            ESP_LOGW(TAG, "mic gain staging failed: %s",
                     esp_err_to_name(gain_err));
        }
    }
    esp_err_t err = audio_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio service start failed: %s", esp_err_to_name(err));
    } else if (settings != NULL) {
        esp_err_t vol_err =
            audio_service_set_output_volume(settings->output_volume);
        if (vol_err != ESP_OK) {
            ESP_LOGW(TAG, "output volume set failed: %s",
                     esp_err_to_name(vol_err));
        }
    }

    err = audio_analysis_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio analysis start failed: %s", esp_err_to_name(err));
    }

#if CONFIG_APP_AUDIO_TEST_TONE
    err = audio_service_selftest_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio self-test start failed: %s", esp_err_to_name(err));
    }
#endif

    err = app_manager_start(disp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "launcher start failed: %s", esp_err_to_name(err));
        abort();
    }

    if (settings != NULL) {
        esp_err_t tz_err = time_service_set_timezone(settings->timezone);
        if (tz_err != ESP_OK) {
            ESP_LOGW(TAG, "timezone apply failed: %s", esp_err_to_name(tz_err));
        }
        esp_err_t cred_err = wifi_probe_set_credentials(
            settings->wifi_ssid, settings->wifi_password);
        if (cred_err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi credential staging failed: %s",
                     esp_err_to_name(cred_err));
        }
    }

    err = wifi_probe_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi task start failed: %s", esp_err_to_name(err));
    }

    err = time_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "time service start failed: %s", esp_err_to_name(err));
    }

    err = ha_client_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HA client start failed: %s", esp_err_to_name(err));
    }

#if CONFIG_APP_HA_TEST
    err = ha_client_selftest_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HA self-test start failed: %s", esp_err_to_name(err));
    }
#endif

    err = temperature_service_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "temperature service init failed: %s",
                 esp_err_to_name(err));
    }

    err = diag_probe_start(disp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostic task start failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "bring-up ready: launcher + display + touch + LVGL + ES7210 + hosted Wi-Fi");
    ESP_LOGI(TAG, "internal heap=%u  PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
