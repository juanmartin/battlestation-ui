#include "temperature_service.h"

#include "driver/temperature_sensor.h"
#include "esp_log.h"

static const char *TAG = "temperature";
static temperature_sensor_handle_t s_sensor;

esp_err_t temperature_service_init(void)
{
    if (s_sensor != NULL) {
        return ESP_OK;
    }

    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t err = temperature_sensor_install(&cfg, &s_sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "install failed: %s", esp_err_to_name(err));
        s_sensor = NULL;
        return err;
    }
    err = temperature_sensor_enable(s_sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable failed: %s", esp_err_to_name(err));
        temperature_sensor_uninstall(s_sensor);
        s_sensor = NULL;
        return err;
    }
    ESP_LOGI(TAG, "internal temperature sensor ready (-10..80 C)");
    return ESP_OK;
}

bool temperature_service_read(float *celsius)
{
    if (celsius == NULL || s_sensor == NULL) {
        return false;
    }
    return temperature_sensor_get_celsius(s_sensor, celsius) == ESP_OK;
}
