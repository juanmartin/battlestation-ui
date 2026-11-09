#include "time_service.h"

#include <stdlib.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_probe.h"

static const char *TAG = "time_service";

/* Any wall-clock time before this is treated as "NTP has not replied yet".
 * 1700000000 is 2023-11-14; a real device never boots into 1970. */
#define TIME_VALID_THRESHOLD ((time_t)1700000000)

static bool s_started;

/* esp_sntp_* posts work to the lwIP TCP/IP thread, which does not exist until
 * wifi_probe's task calls esp_netif_init(). Wait for a connected snapshot in a
 * small background task instead of racing the Wi-Fi bring-up from app_main. */
static void sntp_start_task(void *arg)
{
    (void)arg;

    for (;;) {
        wifi_probe_snapshot_t snap;
        wifi_probe_get(&snap);

        if (snap.state == WIFI_PROBE_CONNECTED) {
            break;
        }
        if (snap.state == WIFI_PROBE_UNPROVISIONED) {
            ESP_LOGW(TAG, "Wi-Fi unprovisioned; clock stays unsynced");
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started (server pool.ntp.org, TZ=%s)", CONFIG_APP_TIMEZONE);
    vTaskDelete(NULL);
}

esp_err_t time_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    if (setenv("TZ", CONFIG_APP_TIMEZONE, 1) != 0) {
        ESP_LOGE(TAG, "failed to apply timezone '%s'", CONFIG_APP_TIMEZONE);
        return ESP_ERR_NO_MEM;
    }
    tzset();

    BaseType_t ok = xTaskCreatePinnedToCore(sntp_start_task, "sntp_start", 3072,
                                            NULL, 3, NULL, 0);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "time service ready (TZ=%s, waiting for Wi-Fi)",
             CONFIG_APP_TIMEZONE);
    return ESP_OK;
}

bool time_service_synced(void)
{
    return time(NULL) >= TIME_VALID_THRESHOLD;
}

bool time_service_get_local(struct tm *out)
{
    if (out == NULL) {
        return false;
    }

    const time_t now = time(NULL);
    if (now < TIME_VALID_THRESHOLD) {
        return false;
    }
    return localtime_r(&now, out) != NULL;
}
