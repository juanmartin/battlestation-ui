#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    WIFI_PROBE_UNPROVISIONED = 0,
    WIFI_PROBE_INITIALIZING,
    WIFI_PROBE_CONNECTING,
    WIFI_PROBE_CONNECTED,
    WIFI_PROBE_FAILED,
} wifi_probe_state_t;

typedef struct {
    wifi_probe_state_t state;
    char ip[16];
    int rssi;
    uint32_t retries;
} wifi_probe_snapshot_t;

esp_err_t wifi_probe_start(void);
void wifi_probe_get(wifi_probe_snapshot_t *out);
