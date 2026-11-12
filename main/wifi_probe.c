#include "wifi_probe.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_probe";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_probe_snapshot_t s_snapshot;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_rssi_timer;
static uint32_t s_retry_count;
static bool s_wifi_started;
static char s_ssid[WIFI_PROBE_SSID_MAX] = CONFIG_APP_WIFI_SSID;
static char s_password[WIFI_PROBE_PASSWORD_MAX] = CONFIG_APP_WIFI_PASSWORD;
static portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_probe_scan_t s_scan;
static wifi_ap_record_t s_scan_records[WIFI_PROBE_SCAN_MAX];

static void snapshot_set_state(wifi_probe_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_snapshot.state = state;
    portEXIT_CRITICAL(&s_lock);
}

static void snapshot_set_ip(const char *ip)
{
    portENTER_CRITICAL(&s_lock);
    if (ip != NULL) {
        strlcpy(s_snapshot.ip, ip, sizeof(s_snapshot.ip));
    } else {
        s_snapshot.ip[0] = '\0';
    }
    portEXIT_CRITICAL(&s_lock);
}

static void copy_ssid(char *dst, size_t size, const char *src)
{
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, size);
}

esp_err_t wifi_probe_set_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t ssid_len = strlen(ssid);
    const size_t password_len = strlen(password);
    if (ssid_len > sizeof(((wifi_config_t *)0)->sta.ssid) ||
        password_len > sizeof(((wifi_config_t *)0)->sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_lock);
    copy_ssid(s_ssid, sizeof(s_ssid), ssid);
    copy_ssid(s_password, sizeof(s_password), password);
    portEXIT_CRITICAL(&s_lock);

    if (!s_wifi_started) {
        return ESP_OK; /* cached for start */
    }

    if (ssid[0] == '\0') {
        esp_wifi_disconnect();
        snapshot_set_state(WIFI_PROBE_UNPROVISIONED);
        snapshot_set_ip(NULL);
        return ESP_OK;
    }

    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, ssid, ssid_len);
    memcpy(wc.sta.password, password, password_len);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(err));
        snapshot_set_state(WIFI_PROBE_FAILED);
        return err;
    }
    s_retry_count = 0;
    snapshot_set_state(WIFI_PROBE_CONNECTING);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect failed: %s", esp_err_to_name(err));
        snapshot_set_state(WIFI_PROBE_FAILED);
        return err;
    }
    ESP_LOGI(TAG, "credentials updated; reconnecting to '%s'", ssid);
    return ESP_OK;
}

esp_err_t wifi_probe_scan_start(void)
{
    if (!s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_scan_lock);
    if (s_scan.running) {
        portEXIT_CRITICAL(&s_scan_lock);
        return ESP_OK;
    }
    s_scan.running = true;
    s_scan.done = false;
    s_scan.count = 0;
    portEXIT_CRITICAL(&s_scan_lock);

    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_scan_lock);
        s_scan.running = false;
        portEXIT_CRITICAL(&s_scan_lock);
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(err));
    }
    return err;
}

void wifi_probe_scan_get(wifi_probe_scan_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_scan_lock);
    *out = s_scan;
    portEXIT_CRITICAL(&s_scan_lock);
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    wifi_probe_snapshot_t snap;
    wifi_probe_get(&snap);
    if (snap.state == WIFI_PROBE_CONNECTED) {
        return;
    }
    s_retry_count++;
    portENTER_CRITICAL(&s_lock);
    s_snapshot.retries = s_retry_count;
    portEXIT_CRITICAL(&s_lock);
    if (s_retry_count <= 5) {
        snapshot_set_state(WIFI_PROBE_CONNECTING);
        ESP_LOGI(TAG, "connect attempt %u", (unsigned)s_retry_count);
        esp_wifi_connect();
        esp_timer_start_once(s_retry_timer, 5ULL * 1000 * 1000);
    } else {
        snapshot_set_state(WIFI_PROBE_FAILED);
        ESP_LOGW(TAG, "connect attempts exhausted; retrying in 15 s");
        s_retry_count = 0;
        esp_timer_start_once(s_retry_timer, 15ULL * 1000 * 1000);
    }
}

static void rssi_timer_cb(void *arg)
{
    (void)arg;
    wifi_probe_snapshot_t snap;
    wifi_probe_get(&snap);
    if (snap.state != WIFI_PROBE_CONNECTED) {
        return;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_snapshot.rssi = ap.rssi;
        portEXIT_CRITICAL(&s_lock);
    }
    esp_timer_start_once(s_rssi_timer, 2ULL * 1000 * 1000);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        snapshot_set_state(WIFI_PROBE_CONNECTING);
        s_retry_count = 1;
        portENTER_CRITICAL(&s_lock);
        s_snapshot.retries = s_retry_count;
        portEXIT_CRITICAL(&s_lock);
        esp_wifi_connect();
        esp_timer_start_once(s_retry_timer, 5ULL * 1000 * 1000);
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "disconnected (reason %d)", d != NULL ? d->reason : -1);
        snapshot_set_state(WIFI_PROBE_CONNECTING);
        snapshot_set_ip(NULL);
        if (!esp_timer_is_active(s_retry_timer)) {
            esp_timer_start_once(s_retry_timer, 2ULL * 1000 * 1000);
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t count = WIFI_PROBE_SCAN_MAX;
        esp_err_t err = esp_wifi_scan_get_ap_records(&count, s_scan_records);
        if (err != ESP_OK) {
            count = 0;
        }
        if (count > WIFI_PROBE_SCAN_MAX) {
            count = WIFI_PROBE_SCAN_MAX;
        }

        portENTER_CRITICAL(&s_scan_lock);
        memset(&s_scan, 0, sizeof(s_scan));
        s_scan.count = count;
        for (uint16_t i = 0; i < count; i++) {
            strlcpy(s_scan.aps[i].ssid, (const char *)s_scan_records[i].ssid,
                    sizeof(s_scan.aps[i].ssid));
            s_scan.aps[i].rssi = s_scan_records[i].rssi;
            s_scan.aps[i].secure = s_scan_records[i].authmode != WIFI_AUTH_OPEN;
        }
        s_scan.running = false;
        s_scan.done = true;
        portEXIT_CRITICAL(&s_scan_lock);
        ESP_LOGI(TAG, "scan done: %u APs", (unsigned)count);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        snapshot_set_ip(ip);
        s_retry_count = 0;
        portENTER_CRITICAL(&s_lock);
        s_snapshot.retries = 0;
        portEXIT_CRITICAL(&s_lock);
        snapshot_set_state(WIFI_PROBE_CONNECTED);
        ESP_LOGI(TAG, "connected, IP %s", ip);
        if (!esp_timer_is_active(s_rssi_timer)) {
            esp_timer_start_once(s_rssi_timer, 100ULL * 1000);
        }
    }
}

static void wifi_init_task(void *arg)
{
    (void)arg;

    char ssid[WIFI_PROBE_SSID_MAX];
    char password[WIFI_PROBE_PASSWORD_MAX];
    portENTER_CRITICAL(&s_lock);
    strlcpy(ssid, s_ssid, sizeof(ssid));
    strlcpy(password, s_password, sizeof(password));
    portEXIT_CRITICAL(&s_lock);

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "no SSID configured; skipping Wi-Fi");
        snapshot_set_state(WIFI_PROBE_UNPROVISIONED);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(ret));
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "check that the ESP32-C6 runs an ESP-Hosted slave matching esp_hosted 1.4.x");
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }

    esp_event_handler_instance_t wifi_instance;
    esp_event_handler_instance_t ip_instance;
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL, &wifi_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WIFI_EVENT handler register failed: %s", esp_err_to_name(ret));
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler, NULL, &ip_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP_EVENT handler register failed: %s", esp_err_to_name(ret));
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }

    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, ssid, strlen(ssid));
    memcpy(wc.sta.password, password, strlen(password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_config(WIFI_IF_STA, &wc);
    }
    if (ret == ESP_OK) {
        /* WIFI_PS_NONE is optional; keep going if the hosted stack rejects it. */
        esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ps_ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_set_ps(WIFI_PS_NONE) returned %s", esp_err_to_name(ps_ret));
        }
        ret = esp_wifi_start();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi STA start failed: %s", esp_err_to_name(ret));
        snapshot_set_state(WIFI_PROBE_FAILED);
        vTaskDelete(NULL);
        return;
    }

    s_wifi_started = true;
    ESP_LOGI(TAG, "STA started for SSID '%s'", ssid);

    /* The event handlers keep running after this task exits. */
    vTaskDelete(NULL);
}

esp_err_t wifi_probe_start(void)
{
    const esp_timer_create_args_t retry_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
    };
    const esp_timer_create_args_t rssi_args = {
        .callback = rssi_timer_cb,
        .name = "wifi_rssi",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &s_retry_timer), TAG, "retry timer");
    ESP_RETURN_ON_ERROR(esp_timer_create(&rssi_args, &s_rssi_timer), TAG, "rssi timer");

    snapshot_set_state(WIFI_PROBE_INITIALIZING);

    BaseType_t ok = xTaskCreatePinnedToCore(wifi_init_task, "wifi_probe", 8192, NULL, 3, NULL, 0);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void wifi_probe_get(wifi_probe_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
}
