#include "ha_client.h"

#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "settings.h"
#include "wifi_probe.h"

static const char *TAG = "ha_client";

/* HA frames are small (a state_changed event is typically < 1.5 KB). Keep one
 * static reassembly buffer for fragmented text frames; the WS client task is
 * the only writer. */
#define HA_WS_RX_CAP 4096
#define HA_REST_RX_CAP 4096
#define HA_CMD_QUEUE_DEPTH 8
#define HA_SYNC_PERIOD_US (30 * 1000 * 1000)
#define HA_HTTP_TIMEOUT_MS 4000
#define HA_WS_TIMEOUT_MS 5000
#define HA_WS_BUFFER_SIZE 4096

static char s_light_entities[HA_LIGHT_COUNT][SETTINGS_ENTITY_MAX] = {
    CONFIG_APP_LIGHT_ENTITY_0,
    CONFIG_APP_LIGHT_ENTITY_1,
    CONFIG_APP_LIGHT_ENTITY_2,
    CONFIG_APP_LIGHT_ENTITY_3,
};
static char s_ha_url[SETTINGS_HA_URL_MAX] = CONFIG_APP_HA_URL;
static char s_ha_token[SETTINGS_HA_TOKEN_MAX] = CONFIG_APP_HA_TOKEN;
static char s_weather_entity[SETTINGS_ENTITY_MAX] = CONFIG_APP_WEATHER_ENTITY;

typedef struct {
    int index;
    bool on;
    int brightness;
    bool sync;
} ha_cmd_t;

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool truncated;
} http_rx_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ha_state_t s_state = HA_OFFLINE;
static ha_light_state_t s_lights[HA_LIGHT_COUNT];
static ha_weather_t s_weather;

static bool s_started;
static volatile bool s_transport_ready;
static volatile bool s_auth_failed;
static char s_rest_base[192];
static char s_ws_uri[224];
static char s_auth_header[256];

static esp_websocket_client_handle_t s_ws;
static QueueHandle_t s_cmd_queue;

/* WebSocket reassembly state; only the WS client task touches it. */
static char s_ws_rx[HA_WS_RX_CAP + 1];
static size_t s_ws_rx_len;
static bool s_ws_rx_overflow;

/* REST response buffer; only the HA worker task touches it. */
static char s_rest_rx[HA_REST_RX_CAP + 1];

static void ha_bringup_task(void *arg);
static void ha_worker_task(void *arg);
static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                             void *event_data);

static bool entity_configured(const char *entity)
{
    return entity != NULL && entity[0] != '\0';
}

static void copy_config_string(char *dst, size_t size, const char *src)
{
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, size);
}

static void load_settings(void)
{
    const app_settings_t *settings = settings_get();
    if (settings == NULL) {
        return; /* compile-time defaults are already in the buffers */
    }
    copy_config_string(s_ha_url, sizeof(s_ha_url), settings->ha_url);
    copy_config_string(s_ha_token, sizeof(s_ha_token), settings->ha_token);
    copy_config_string(s_weather_entity, sizeof(s_weather_entity),
                       settings->weather_entity);
    for (int i = 0; i < HA_LIGHT_COUNT; i++) {
        copy_config_string(s_light_entities[i], SETTINGS_ENTITY_MAX,
                           settings->light_entity[i]);
    }
}

static bool configured(void)
{
    /* Both URL and token are required. The token is never logged. */
    return s_ha_url[0] != '\0' && s_ha_token[0] != '\0';
}

static bool any_entity_configured(void)
{
    for (int i = 0; i < HA_LIGHT_COUNT; i++) {
        if (entity_configured(s_light_entities[i])) {
            return true;
        }
    }
    return entity_configured(s_weather_entity);
}

static int light_index_for_entity(const char *entity_id)
{
    if (entity_id == NULL) {
        return -1;
    }
    for (int i = 0; i < HA_LIGHT_COUNT; i++) {
        if (entity_configured(s_light_entities[i]) &&
            strcmp(s_light_entities[i], entity_id) == 0) {
            return i;
        }
    }
    return -1;
}

static void set_state(ha_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
}

static void mark_light_unavailable(int index)
{
    if (index < 0 || index >= HA_LIGHT_COUNT) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_lights[index].available = false;
    portEXIT_CRITICAL(&s_lock);
}

static void mark_weather_unavailable(void)
{
    portENTER_CRITICAL(&s_lock);
    s_weather.available = false;
    portEXIT_CRITICAL(&s_lock);
}

static bool build_config(void)
{
    const char *url = s_ha_url;
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        len--;
    }

    if (strncmp(url, "https://", 8) == 0 || strncmp(url, "wss://", 6) == 0) {
        ESP_LOGE(TAG, "HTTPS/TLS not enabled in M7b; staying offline");
        return false;
    }

    size_t scheme_len;
    if (strncmp(url, "http://", 7) == 0) {
        scheme_len = 7;
    } else if (strncmp(url, "ws://", 5) == 0) {
        scheme_len = 5;
    } else {
        ESP_LOGE(TAG, "HA URL must start with http:// (or ws://); staying offline");
        return false;
    }
    if (len <= scheme_len || url[scheme_len] == '/') {
        ESP_LOGE(TAG, "HA URL has no host; staying offline");
        return false;
    }

    const size_t host_len = len - scheme_len;
    if (host_len >= sizeof(s_rest_base) - sizeof("http://") ||
        host_len >= sizeof(s_ws_uri) - sizeof("ws:///api/websocket")) {
        ESP_LOGE(TAG, "HA URL is too long; staying offline");
        return false;
    }

    char host[sizeof(s_ws_uri)];
    memcpy(host, url + scheme_len, host_len);
    host[host_len] = '\0';

    strlcpy(s_rest_base, "http://", sizeof(s_rest_base));
    if (strlcat(s_rest_base, host, sizeof(s_rest_base)) >= sizeof(s_rest_base)) {
        ESP_LOGE(TAG, "HA REST URL is too long; staying offline");
        return false;
    }

    strlcpy(s_ws_uri, "ws://", sizeof(s_ws_uri));
    if (strlcat(s_ws_uri, host, sizeof(s_ws_uri)) >= sizeof(s_ws_uri) ||
        strlcat(s_ws_uri, "/api/websocket", sizeof(s_ws_uri)) >= sizeof(s_ws_uri)) {
        ESP_LOGE(TAG, "HA WebSocket URL is too long; staying offline");
        return false;
    }

    strlcpy(s_auth_header, "Bearer ", sizeof(s_auth_header));
    if (strlcat(s_auth_header, s_ha_token, sizeof(s_auth_header)) >=
        sizeof(s_auth_header)) {
        ESP_LOGE(TAG, "HA token is too long; staying offline");
        return false;
    }
    return true;
}

static bool parse_brightness(const cJSON *attributes, int *out)
{
    if (!cJSON_IsObject(attributes) || out == NULL) {
        return false;
    }

    const cJSON *pct = cJSON_GetObjectItemCaseSensitive(attributes, "brightness_pct");
    if (cJSON_IsNumber(pct)) {
        int value = (int)(pct->valuedouble + 0.5);
        if (value < 0) {
            value = 0;
        } else if (value > 100) {
            value = 100;
        }
        *out = value;
        return true;
    }

    const cJSON *raw = cJSON_GetObjectItemCaseSensitive(attributes, "brightness");
    if (cJSON_IsNumber(raw)) {
        int value = (int)(raw->valuedouble + 0.5);
        if (value < 0) {
            value = 0;
        } else if (value > 255) {
            value = 255;
        }
        *out = (value * 100 + 127) / 255;
        return true;
    }
    return false;
}

static void apply_light_json(int index, const cJSON *state_obj)
{
    if (index < 0 || index >= HA_LIGHT_COUNT) {
        return;
    }

    bool available = false;
    bool on = false;
    int brightness = 0;
    bool have_brightness = false;

    if (cJSON_IsObject(state_obj)) {
        const cJSON *state = cJSON_GetObjectItemCaseSensitive(state_obj, "state");
        const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(state_obj, "attributes");

        if (cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "on") == 0) {
                on = true;
                available = true;
            } else if (strcmp(state->valuestring, "off") == 0) {
                available = true;
            }
        }
        if (available) {
            have_brightness = parse_brightness(attributes, &brightness);
        }
    }

    portENTER_CRITICAL(&s_lock);
    ha_light_state_t *dst = &s_lights[index];
    const bool changed = !dst->available || dst->on != on ||
                         (available && dst->brightness != brightness);
    if (!available) {
        dst->available = false;
    } else {
        if (!have_brightness) {
            brightness = dst->brightness;
            if (on && brightness <= 0) {
                brightness = 100;
            }
        }
        if (on && brightness < 1) {
            brightness = 1;
        }
        dst->on = on;
        dst->brightness = brightness;
        dst->available = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (available && changed) {
        ESP_LOGI(TAG, "light %d: %s %d%%", index, on ? "on" : "off", brightness);
    }
}

static void apply_weather_json(const cJSON *state_obj)
{
    bool available = false;
    float temperature_c = 0.0f;
    char condition[sizeof(s_weather.condition)] = {0};

    if (cJSON_IsObject(state_obj)) {
        const cJSON *state = cJSON_GetObjectItemCaseSensitive(state_obj, "state");
        const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(state_obj, "attributes");
        const cJSON *temperature = cJSON_IsObject(attributes)
                                       ? cJSON_GetObjectItemCaseSensitive(attributes, "temperature")
                                       : NULL;

        if (cJSON_IsString(state) && cJSON_IsNumber(temperature)) {
            available = true;
            temperature_c = (float)temperature->valuedouble;
            strlcpy(condition, state->valuestring, sizeof(condition));
        }
    }

    portENTER_CRITICAL(&s_lock);
    const bool changed = !s_weather.available || s_weather.temperature_c != temperature_c ||
                         strcmp(s_weather.condition, condition) != 0;
    s_weather.available = available;
    if (available) {
        s_weather.temperature_c = temperature_c;
        memcpy(s_weather.condition, condition, sizeof(s_weather.condition));
    }
    portEXIT_CRITICAL(&s_lock);

    if (available && changed) {
        ESP_LOGI(TAG, "weather: %s %.1f C", condition, temperature_c);
    }
}

static esp_err_t http_event_cb(esp_http_client_event_t *event)
{
    http_rx_t *rx = (http_rx_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || rx == NULL || rx->buf == NULL ||
        rx->cap <= 1 || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    const size_t available = rx->cap - 1 - rx->len;
    size_t copy = (size_t)event->data_len;
    if (copy > available) {
        copy = available;
        rx->truncated = true;
    }
    if (copy > 0) {
        memcpy(rx->buf + rx->len, event->data, copy);
        rx->len += copy;
    }
    return ESP_OK;
}

static esp_err_t rest_request(const char *method, const char *path, const char *body,
                              char *rx, size_t rx_cap)
{
    char url[256];
    strlcpy(url, s_rest_base, sizeof(url));
    if (strlcat(url, path, sizeof(url)) >= sizeof(url)) {
        ESP_LOGE(TAG, "REST URL too long");
        return ESP_ERR_INVALID_SIZE;
    }

    http_rx_t rxbuf = {
        .buf = rx,
        .cap = rx_cap,
        .len = 0,
        .truncated = false,
    };
    if (rx != NULL && rx_cap > 0) {
        rx[0] = '\0';
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = HA_HTTP_TIMEOUT_MS,
        .buffer_size = HA_REST_RX_CAP,
        .buffer_size_tx = 2048,
        .disable_auto_redirect = true,
        .event_handler = http_event_cb,
        .user_data = &rxbuf,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Authorization", s_auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");
    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, body != NULL ? (int)strlen(body) : 0);
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    }

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);

    if (rx != NULL && rx_cap > 0) {
        rx[rxbuf.len < rx_cap ? rxbuf.len : rx_cap - 1] = '\0';
    }
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "REST %s %s failed: %s", method, path, esp_err_to_name(err));
        return err;
    }
    if (status == 401 || status == 403) {
        ESP_LOGW(TAG, "REST authentication rejected (status %d)", status);
        return ESP_ERR_INVALID_STATE;
    }
    if (status == 404) {
        return ESP_ERR_NOT_FOUND;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "REST %s %s status %d", method, path, status);
        return ESP_FAIL;
    }
    if (rx != NULL && rxbuf.truncated) {
        ESP_LOGW(TAG, "REST response truncated for %s", path);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t rest_get_entity(const char *entity, cJSON **out)
{
    if (out == NULL || !entity_configured(entity)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    char path[192];
    strlcpy(path, "/api/states/", sizeof(path));
    if (strlcat(path, entity, sizeof(path)) >= sizeof(path)) {
        ESP_LOGE(TAG, "HA entity ID is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = rest_request("GET", path, NULL, s_rest_rx, sizeof(s_rest_rx));
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(s_rest_rx);
    if (root == NULL) {
        ESP_LOGW(TAG, "Invalid JSON for %s", entity);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = root;
    return ESP_OK;
}

static bool sync_all(void)
{
    if (s_auth_failed) {
        return false;
    }

    bool transport_ok = true;

    for (int i = 0; i < HA_LIGHT_COUNT; i++) {
        const char *entity = s_light_entities[i];
        if (!entity_configured(entity)) {
            continue;
        }

        cJSON *root = NULL;
        esp_err_t err = rest_get_entity(entity, &root);
        if (err == ESP_OK) {
            apply_light_json(i, root);
            cJSON_Delete(root);
        } else if (err == ESP_ERR_NOT_FOUND) {
            mark_light_unavailable(i);
        } else {
            transport_ok = false;
            if (err == ESP_ERR_INVALID_STATE) {
                s_auth_failed = true;
                s_transport_ready = false;
            }
            break;
        }
    }

    if (transport_ok) {
        const char *weather_entity = s_weather_entity;
        if (entity_configured(weather_entity)) {
            cJSON *root = NULL;
            esp_err_t err = rest_get_entity(weather_entity, &root);
            if (err == ESP_OK) {
                apply_weather_json(root);
                cJSON_Delete(root);
            } else if (err == ESP_ERR_NOT_FOUND) {
                mark_weather_unavailable();
            } else {
                transport_ok = false;
                if (err == ESP_ERR_INVALID_STATE) {
                    s_auth_failed = true;
                    s_transport_ready = false;
                }
            }
        }
    }

    if (transport_ok) {
        set_state(HA_CONNECTED);
    } else if (s_auth_failed) {
        set_state(HA_OFFLINE);
    } else {
        set_state(HA_CONNECTING);
    }
    return transport_ok;
}

static void rest_post_command(const ha_cmd_t *cmd)
{
    if (cmd == NULL || cmd->index < 0 || cmd->index >= HA_LIGHT_COUNT) {
        return;
    }
    const char *entity = s_light_entities[cmd->index];
    if (!entity_configured(entity)) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "entity_id", entity);
    if (cmd->on && cmd->brightness > 0) {
        cJSON_AddNumberToObject(root, "brightness_pct", cmd->brightness);
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return;
    }

    const char *path = cmd->on ? "/api/services/light/turn_on"
                               : "/api/services/light/turn_off";
    esp_err_t err = rest_request("POST", path, body, NULL, 0);
    cJSON_free(body);

    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_lights[cmd->index].on = cmd->on;
        s_lights[cmd->index].brightness = cmd->brightness;
        s_lights[cmd->index].available = true;
        portEXIT_CRITICAL(&s_lock);
        set_state(HA_CONNECTED);
    } else if (err == ESP_ERR_INVALID_STATE) {
        s_auth_failed = true;
        s_transport_ready = false;
        set_state(HA_OFFLINE);
    }
    /* Other failures are retried by the next periodic REST sync. */
}

static void ha_worker_task(void *arg)
{
    (void)arg;

    int64_t next_sync_us = esp_timer_get_time();

    for (;;) {
        int64_t now = esp_timer_get_time();
        if (now >= next_sync_us) {
            sync_all();
            next_sync_us = esp_timer_get_time() + HA_SYNC_PERIOD_US;
            now = esp_timer_get_time();
        }

        int64_t wait_ms = (next_sync_us - now) / 1000;
        if (wait_ms > 1000) {
            wait_ms = 1000;
        } else if (wait_ms < 1) {
            wait_ms = 1;
        }

        ha_cmd_t cmd;
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS((uint32_t)wait_ms)) == pdTRUE) {
            if (cmd.sync) {
                sync_all();
                next_sync_us = esp_timer_get_time() + HA_SYNC_PERIOD_US;
            } else {
                rest_post_command(&cmd);
            }
        }
    }
}

static void ws_send_json(cJSON *root)
{
    if (root == NULL || s_ws == NULL || !esp_websocket_client_is_connected(s_ws)) {
        cJSON_Delete(root);
        return;
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return;
    }

    int sent = esp_websocket_client_send_text(s_ws, payload, (int)strlen(payload),
                                              pdMS_TO_TICKS(1000));
    cJSON_free(payload);
    if (sent < 0) {
        ESP_LOGW(TAG, "WebSocket send failed");
    }
}

static void ws_send_auth(void)
{
    if (s_auth_failed) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "type", "auth");
    cJSON_AddStringToObject(root, "access_token", s_ha_token);
    ws_send_json(root);
}

static void ws_send_subscribe(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddNumberToObject(root, "id", 1);
    cJSON_AddStringToObject(root, "type", "subscribe_events");
    cJSON_AddStringToObject(root, "event_type", "state_changed");
    ws_send_json(root);
}

static void request_rest_sync(void)
{
    if (s_cmd_queue == NULL) {
        return;
    }
    ha_cmd_t cmd = {
        .index = -1,
        .sync = true,
    };
    (void)xQueueSend(s_cmd_queue, &cmd, 0);
}

static void ws_handle_event(const cJSON *root)
{
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (!cJSON_IsObject(event)) {
        return;
    }

    const cJSON *event_type = cJSON_GetObjectItemCaseSensitive(event, "event_type");
    if (cJSON_IsString(event_type) && strcmp(event_type->valuestring, "state_changed") != 0) {
        return;
    }

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(event, "data");
    if (!cJSON_IsObject(data)) {
        return;
    }

    const cJSON *entity_id = cJSON_GetObjectItemCaseSensitive(data, "entity_id");
    if (!cJSON_IsString(entity_id)) {
        return;
    }

    const cJSON *new_state = cJSON_GetObjectItemCaseSensitive(data, "new_state");
    const int light_index = light_index_for_entity(entity_id->valuestring);
    if (light_index >= 0) {
        apply_light_json(light_index, new_state);
        return;
    }
    if (entity_configured(s_weather_entity) &&
        strcmp(s_weather_entity, entity_id->valuestring) == 0) {
        apply_weather_json(new_state);
    }
}

static void ws_handle_text(const char *text)
{
    cJSON *root = cJSON_Parse(text);
    if (root == NULL) {
        ESP_LOGW(TAG, "WebSocket JSON parse failed");
        return;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type)) {
        const char *value = type->valuestring;
        if (strcmp(value, "auth_required") == 0) {
            ws_send_auth();
        } else if (strcmp(value, "auth_ok") == 0) {
            if (!s_auth_failed) {
                set_state(HA_CONNECTED);
                ws_send_subscribe();
                request_rest_sync();
                ESP_LOGI(TAG, "WebSocket authenticated");
            }
        } else if (strcmp(value, "auth_invalid") == 0) {
            if (!s_auth_failed) {
                ESP_LOGE(TAG, "Home Assistant authentication failed; check token");
            }
            s_auth_failed = true;
            s_transport_ready = false;
            set_state(HA_OFFLINE);
        } else if (strcmp(value, "event") == 0) {
            ws_handle_event(root);
        }
    }
    cJSON_Delete(root);
}

static void ws_handle_frame(const esp_websocket_event_data_t *data)
{
    if (data == NULL) {
        return;
    }

    if (data->op_code == 0x01 && data->payload_offset == 0) {
        s_ws_rx_len = 0;
        s_ws_rx_overflow = false;
    } else if (data->op_code != 0x00 && data->op_code != 0x01) {
        /* Control frames may interleave with a fragmented text message. */
        return;
    }

    if (data->data_len > 0) {
        if (s_ws_rx_len + (size_t)data->data_len > HA_WS_RX_CAP) {
            s_ws_rx_overflow = true;
        } else {
            memcpy(s_ws_rx + s_ws_rx_len, data->data_ptr, (size_t)data->data_len);
            s_ws_rx_len += (size_t)data->data_len;
        }
    }

    if (data->fin) {
        if (s_ws_rx_overflow) {
            ESP_LOGW(TAG, "WebSocket message exceeds %d bytes; dropped", HA_WS_RX_CAP);
        } else if (s_ws_rx_len > 0) {
            s_ws_rx[s_ws_rx_len] = '\0';
            ws_handle_text(s_ws_rx);
        }
        s_ws_rx_len = 0;
        s_ws_rx_overflow = false;
    }
}

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                             void *event_data)
{
    (void)arg;
    (void)base;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_ws_rx_len = 0;
        s_ws_rx_overflow = false;
        if (!s_auth_failed) {
            set_state(HA_CONNECTING);
        }
        ESP_LOGI(TAG, "WebSocket connected");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        s_ws_rx_len = 0;
        s_ws_rx_overflow = false;
        if (!s_auth_failed) {
            set_state(HA_CONNECTING);
            ESP_LOGW(TAG, "WebSocket disconnected");
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        s_ws_rx_len = 0;
        s_ws_rx_overflow = false;
        if (!s_auth_failed) {
            set_state(HA_CONNECTING);
            ESP_LOGW(TAG, "WebSocket error");
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        ws_handle_frame((const esp_websocket_event_data_t *)event_data);
        break;
    default:
        break;
    }
}

static bool start_transport(void)
{
    s_cmd_queue = xQueueCreate(HA_CMD_QUEUE_DEPTH, sizeof(ha_cmd_t));
    if (s_cmd_queue == NULL) {
        ESP_LOGE(TAG, "command queue allocation failed");
        return false;
    }

    s_transport_ready = true;
    set_state(HA_CONNECTING);

    BaseType_t ok = xTaskCreatePinnedToCore(ha_worker_task, "ha_worker", 8192,
                                            NULL, 3, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "HA worker task creation failed");
        s_transport_ready = false;
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        return false;
    }

    esp_websocket_client_config_t ws_config = {
        .uri = s_ws_uri,
        .buffer_size = HA_WS_BUFFER_SIZE,
        .reconnect_timeout_ms = HA_WS_TIMEOUT_MS,
        .network_timeout_ms = HA_WS_TIMEOUT_MS,
        .disable_auto_reconnect = false,
        .enable_close_reconnect = true,
        .task_core_id_set = true,
        .task_core_id = 0,
        .task_prio = 3,
        .task_stack = 6144,
        .task_name = "ha_ws",
    };

    s_ws = esp_websocket_client_init(&ws_config);
    if (s_ws == NULL) {
        ESP_LOGE(TAG, "WebSocket init failed; REST polling remains active");
    } else {
        esp_err_t err = esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                                      ws_event_handler, NULL);
        if (err == ESP_OK) {
            err = esp_websocket_client_start(s_ws);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WebSocket start failed: %s; REST remains active",
                     esp_err_to_name(err));
            esp_websocket_client_destroy(s_ws);
            s_ws = NULL;
        }
    }

    ESP_LOGI(TAG, "Home Assistant transport started (WebSocket + REST)");
    return true;
}

static void ha_bringup_task(void *arg)
{
    (void)arg;

    /* esp_http_client/esp_websocket_client create sockets and the WS client
     * posts to lwIP. wifi_probe owns lwIP startup, so wait until it reports a
     * usable connection before starting either transport. */
    for (;;) {
        wifi_probe_snapshot_t snapshot;
        wifi_probe_get(&snapshot);

        if (snapshot.state == WIFI_PROBE_CONNECTED) {
            break;
        }
        if (snapshot.state == WIFI_PROBE_UNPROVISIONED) {
            ESP_LOGW(TAG, "Wi-Fi unprovisioned; HA stays offline");
            set_state(HA_OFFLINE);
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (!start_transport()) {
        set_state(HA_OFFLINE);
    }
    vTaskDelete(NULL);
}

esp_err_t ha_client_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_started = true;
    load_settings();

    /* The HTTP client prints the full request header buffer at DEBUG level,
     * including Authorization. Keep that tag at INFO so the token cannot leak
     * into a debug log. */
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_INFO);

    if (!configured()) {
        set_state(HA_OFFLINE);
        ESP_LOGI(TAG, "Home Assistant not configured; dashboard cards run offline");
        return ESP_OK;
    }

    if (!build_config()) {
        set_state(HA_OFFLINE);
        return ESP_OK;
    }

    if (!any_entity_configured()) {
        set_state(HA_OFFLINE);
        ESP_LOGW(TAG, "No HA light or weather entities configured; staying offline");
        return ESP_OK;
    }

    set_state(HA_CONNECTING);
    BaseType_t ok = xTaskCreatePinnedToCore(ha_bringup_task, "ha_bringup", 6144,
                                            NULL, 3, NULL, 0);
    if (ok != pdPASS) {
        set_state(HA_OFFLINE);
        ESP_LOGE(TAG, "HA bring-up task creation failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Home Assistant configured; waiting for Wi-Fi");
    return ESP_OK;
}

ha_state_t ha_client_state(void)
{
    portENTER_CRITICAL(&s_lock);
    const ha_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
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

    portENTER_CRITICAL(&s_lock);
    *out = s_lights[index];
    const bool available = out->available;
    portEXIT_CRITICAL(&s_lock);
    return available;
}

bool ha_client_get_weather(ha_weather_t *out)
{
    if (out == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    *out = s_weather;
    const bool available = out->available;
    portEXIT_CRITICAL(&s_lock);
    return available;
}

esp_err_t ha_client_set_light(int index, bool on, int brightness)
{
    if (index < 0 || index >= HA_LIGHT_COUNT || brightness < 0 || brightness > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!configured() || !s_transport_ready || s_cmd_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!entity_configured(s_light_entities[index])) {
        return ESP_ERR_INVALID_STATE;
    }

    ha_cmd_t cmd = {
        .index = index,
        .on = on,
        .brightness = brightness,
        .sync = false,
    };
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
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
    return s_weather_entity;
}
