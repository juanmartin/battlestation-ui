#include "ha_client_selftest.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ha_client.h"

static const char *TAG = "ha_selftest";
static bool s_started;

static void ha_selftest_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(8000));

    ESP_LOGI(TAG, "light 0 -> on 40%%");
    esp_err_t err = ha_client_set_light(0, true, 40);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "on command failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "light 0 -> off");
    err = ha_client_set_light(0, false, 40);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "off command failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "done");
    vTaskDelete(NULL);
}

esp_err_t ha_client_selftest_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(ha_selftest_task, "ha_selftest",
                                            4096, NULL, 3, NULL, 1);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}
