#include "ha_client.h"

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ha_client";

static const char *const s_light_entities[HA_LIGHT_COUNT] = {
    CONFIG_APP_LIGHT_ENTITY_0,
    CONFIG_APP_LIGHT_ENTITY_1,
    CONFIG_APP_LIGHT_ENTITY_2,
    CONFIG_APP_LIGHT_ENTITY_3,
};

static ha_state_t s_state = HA_OFFLINE;
static ha_light_state_t s_lights[HA_LIGHT_COUNT];

static bool configured(void)
{
    /* Both URL and token are required. The token is never logged. */
    return CONFIG_APP_HA_URL[0] != '\0' && CONFIG_APP_HA_TOKEN[0] != '\0';
}

esp_err_t ha_client_start(void)
{
    if (!configured()) {
        s_state = HA_OFFLINE;
        ESP_LOGI(TAG, "Home Assistant not configured; dashboard cards run offline");
        return ESP_OK;
    }

    /* M7b replaces this with the WebSocket/REST client. Until then the honest
     * state is "connecting": no entity state is available. */
    s_state = HA_CONNECTING;
    ESP_LOGI(TAG, "Home Assistant configured; client stub has no transport yet");
    return ESP_OK;
}

ha_state_t ha_client_state(void)
{
    return s_state;
}

bool ha_client_configured(void)
{
    return configured();
}

bool ha_client_get_light(int index, ha_light_state_t *out)
{
    if (out == NULL || index < 0 || index >= HA_LIGHT_COUNT) {
        return false;
    }

    *out = s_lights[index];
    if (s_state != HA_CONNECTED) {
        out->available = false;
    }
    return out->available;
}

esp_err_t ha_client_set_light(int index, bool on, int brightness)
{
    if (index < 0 || index >= HA_LIGHT_COUNT || brightness < 0 || brightness > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!configured()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state != HA_CONNECTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_lights[index].on = on;
    s_lights[index].brightness = brightness;
    s_lights[index].available = true;
    return ESP_OK;
}

const char *ha_client_light_entity(int index)
{
    if (index < 0 || index >= HA_LIGHT_COUNT) {
        return "";
    }
    return s_light_entities[index];
}

const char *ha_client_weather_entity(void)
{
    return CONFIG_APP_WEATHER_ENTITY;
}
