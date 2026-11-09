#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define HA_LIGHT_COUNT 4

typedef enum {
    HA_OFFLINE,
    HA_CONNECTING,
    HA_CONNECTED,
} ha_state_t;

typedef struct {
    bool on;
    int brightness;   /* 0..100 */
    bool available;
} ha_light_state_t;

/* M7a stub: validates configuration and publishes the offline/connecting state.
 * The WebSocket/REST transport lands in M7b. */
esp_err_t ha_client_start(void);
ha_state_t ha_client_state(void);
bool ha_client_configured(void);
bool ha_client_get_light(int index, ha_light_state_t *out);
esp_err_t ha_client_set_light(int index, bool on, int brightness);
const char *ha_client_light_entity(int index);
const char *ha_client_weather_entity(void);
