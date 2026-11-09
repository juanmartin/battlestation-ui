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

typedef struct {
    bool available;
    float temperature_c;
    char condition[32];
} ha_weather_t;

/* M7b: WebSocket state push plus REST polling/commands over plain LAN HTTP.
 * TLS is intentionally not enabled; https:// URLs stay offline. */
esp_err_t ha_client_start(void);
ha_state_t ha_client_state(void);
bool ha_client_configured(void);
bool ha_client_get_light(int index, ha_light_state_t *out);
bool ha_client_get_weather(ha_weather_t *out);

/* Queue a light command. Never blocks the caller; the worker task performs the
 * HTTP POST. Returns ESP_ERR_INVALID_STATE when HA/entity is not configured and
 * ESP_ERR_TIMEOUT when the command queue is full. */
esp_err_t ha_client_set_light(int index, bool on, int brightness);
const char *ha_client_light_entity(int index);
const char *ha_client_weather_entity(void);
