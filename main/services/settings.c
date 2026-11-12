#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "settings_codec.h"

static const char *TAG = "settings";
#define SETTINGS_NAMESPACE "battlestation"
#define SETTINGS_KEY "cfg"

static app_settings_t s_settings;
static nvs_handle_t s_nvs;
static bool s_ready;

static void copy_string(char *dst, size_t size, const char *src)
{
    if (dst == NULL || size == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, size, "%s", src);
}

static void fill_kconfig_defaults(app_settings_t *settings)
{
    settings_codec_defaults(settings);
    settings->brightness = CONFIG_APP_BACKLIGHT_PERCENT;
    settings->mic_gain_db = CONFIG_APP_MIC_GAIN_DB;
    copy_string(settings->wifi_ssid, sizeof(settings->wifi_ssid),
                CONFIG_APP_WIFI_SSID);
    copy_string(settings->wifi_password, sizeof(settings->wifi_password),
                CONFIG_APP_WIFI_PASSWORD);
    copy_string(settings->ha_url, sizeof(settings->ha_url), CONFIG_APP_HA_URL);
    copy_string(settings->ha_token, sizeof(settings->ha_token),
                CONFIG_APP_HA_TOKEN);
    copy_string(settings->timezone, sizeof(settings->timezone),
                CONFIG_APP_TIMEZONE);
    copy_string(settings->light_entity[0], SETTINGS_ENTITY_MAX,
                CONFIG_APP_LIGHT_ENTITY_0);
    copy_string(settings->light_entity[1], SETTINGS_ENTITY_MAX,
                CONFIG_APP_LIGHT_ENTITY_1);
    copy_string(settings->light_entity[2], SETTINGS_ENTITY_MAX,
                CONFIG_APP_LIGHT_ENTITY_2);
    copy_string(settings->light_entity[3], SETTINGS_ENTITY_MAX,
                CONFIG_APP_LIGHT_ENTITY_3);
    copy_string(settings->weather_entity, sizeof(settings->weather_entity),
                CONFIG_APP_WEATHER_ENTITY);
    settings_codec_validate(settings);
}

static esp_err_t save_current(void)
{
    if (!s_ready || s_nvs == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t blob[sizeof(settings_codec_header_t) + sizeof(app_settings_t) + 4u];
    size_t length =
        settings_codec_serialize(&s_settings, blob, sizeof(blob));
    if (length == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = nvs_set_blob(s_nvs, SETTINGS_KEY, blob, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t settings_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s; using RAM defaults",
                 esp_err_to_name(err));
        fill_kconfig_defaults(&s_settings);
        s_ready = true;
        return ESP_OK;
    }

    err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s; using RAM defaults",
                 esp_err_to_name(err));
        fill_kconfig_defaults(&s_settings);
        s_ready = true;
        return ESP_OK;
    }

    app_settings_t loaded;
    uint8_t blob[sizeof(settings_codec_header_t) + sizeof(app_settings_t) + 4u];
    size_t length = sizeof(blob);
    err = nvs_get_blob(s_nvs, SETTINGS_KEY, blob, &length);
    bool have_blob = (err == ESP_OK &&
                      settings_codec_deserialize(&loaded, blob, length));
    if (have_blob) {
        s_settings = loaded;
        settings_codec_validate(&s_settings);
    } else {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "invalid settings blob (%s); using defaults",
                     esp_err_to_name(err));
        }
        fill_kconfig_defaults(&s_settings);
    }

    s_ready = true;
    if (!have_blob) {
        (void)save_current();
    }

    ESP_LOGI(TAG, "ready: source=%s brightness=%d mic=%d volume=%d a4=%.1f "
                  "ha=%s wifi=%s",
             have_blob ? "nvs" : "defaults", s_settings.brightness,
             s_settings.mic_gain_db, s_settings.output_volume,
             (double)s_settings.a4_hz,
             s_settings.ha_url[0] != '\0' ? "configured" : "empty",
             s_settings.wifi_ssid[0] != '\0' ? "configured" : "empty");
    return ESP_OK;
}

const app_settings_t *settings_get(void)
{
    return s_ready ? &s_settings : NULL;
}

esp_err_t settings_update(const app_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_settings = *settings;
    settings_codec_validate(&s_settings);
    return ESP_OK;
}

esp_err_t settings_save(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return save_current();
}

esp_err_t settings_reset_defaults(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    fill_kconfig_defaults(&s_settings);
    return save_current();
}
