#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_PROBE_SCAN_MAX 12
#define WIFI_PROBE_SSID_MAX 33
#define WIFI_PROBE_PASSWORD_MAX 65

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

typedef struct {
    char ssid[WIFI_PROBE_SSID_MAX];
    int8_t rssi;
    bool secure;
} wifi_probe_scan_ap_t;

typedef struct {
    bool running;
    bool done;
    size_t count;
    wifi_probe_scan_ap_t aps[WIFI_PROBE_SCAN_MAX];
} wifi_probe_scan_t;

esp_err_t wifi_probe_start(void);
void wifi_probe_get(wifi_probe_snapshot_t *out);

/* Runtime credentials. Safe before start (cached) or after start (reconnect). */
esp_err_t wifi_probe_set_credentials(const char *ssid, const char *password);

/* Async scan of nearby 2.4 GHz APs; poll wifi_probe_scan_get(). */
esp_err_t wifi_probe_scan_start(void);
void wifi_probe_scan_get(wifi_probe_scan_t *out);
