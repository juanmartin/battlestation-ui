#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "audio_analysis.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ha_client.h"
#include "sdkconfig.h"
#include "time_service.h"
#include "ui_theme.h"
#include "wifi_probe.h"

static const char *TAG = "dashboard";

#define COLOR_BG 0x0B0F14
#define COLOR_TEXT 0xE2E8F0
#define COLOR_MUTED 0x94A3B8
#define COLOR_DIM 0x64748B
#define COLOR_ACCENT 0x22D3EE
#define COLOR_GREEN 0x4ADE80
#define COLOR_AMBER 0xFBBF24
#define COLOR_RED 0xF87171

#define TIMER_PERIOD_MS 100
/* Audio stays at 10 Hz. HA pushes should reach the labels in well under 1 s;
 * clock and Wi-Fi do not need to run that often. */
#define HA_TICK_DIVIDER 5u
#define SLOW_TICK_DIVIDER 10u

enum {
    CARD_CLOCK = 0,
    CARD_WEATHER,
    CARD_STATUS,
    CARD_LIGHT_0,
};

typedef struct {
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *sync;
} clock_ui_t;

typedef struct {
    lv_obj_t *value;
    lv_obj_t *condition;
    lv_obj_t *entity;
    lv_obj_t *status;
} weather_ui_t;

typedef struct {
    lv_obj_t *wifi;
    lv_obj_t *ip;
    lv_obj_t *rssi;
    lv_obj_t *bar;
    lv_obj_t *dbfs;
} status_ui_t;

typedef struct {
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *tz;
    lv_obj_t *sync;
} clock_detail_ui_t;

typedef struct {
    lv_obj_t *value;
    lv_obj_t *entity;
    lv_obj_t *condition;
    lv_obj_t *status;
} weather_detail_ui_t;

typedef struct {
    lv_obj_t *wifi;
    lv_obj_t *ip;
    lv_obj_t *rssi;
    lv_obj_t *retries;
    lv_obj_t *heap;
    lv_obj_t *dbfs_l;
    lv_obj_t *dbfs_r;
    lv_obj_t *dbfs_avg;
    lv_obj_t *dbfs_peak;
    lv_obj_t *bar_l;
    lv_obj_t *bar_r;
} status_detail_ui_t;

typedef struct {
    int index;
    lv_obj_t *state;
    lv_obj_t *power_label;
    lv_obj_t *slider;
    lv_obj_t *brightness_label;
    lv_obj_t *hint;

    lv_obj_t *detail_state;
    lv_obj_t *detail_power_label;
    lv_obj_t *detail_slider;
    lv_obj_t *detail_brightness_label;
    lv_obj_t *detail_hint;

    bool on;
    int brightness;
    bool remote_valid;
    bool remote_on;
    bool pending;
    uint32_t pending_until_ms;
    bool applying;
} light_ui_t;

typedef struct {
    lv_obj_t *dash;
    lv_obj_t *grid;
    lv_obj_t *detail;
    int detail_kind;
    lv_obj_t *header_date;

    clock_ui_t clock;
    weather_ui_t weather;
    status_ui_t status;

    clock_detail_ui_t clock_detail;
    weather_detail_ui_t weather_detail;
    status_detail_ui_t status_detail;
} dashboard_ui_t;

static dashboard_ui_t s_ui;
static light_ui_t s_lights[HA_LIGHT_COUNT];
static lv_timer_t *s_timer;
static uint32_t s_ticks;

/* Deferred detail open/close. LVGL async calls run outside the event callback,
 * so a BACK button or a card is never deleted while its own callback runs. */
static bool s_async_pending;
static bool s_open_requested;
static bool s_close_requested;
static int s_requested_kind;

static void detail_request_open(int kind);
static void detail_request_close(void);

static int dbfs_to_percent(float dbfs)
{
    if (dbfs <= -96.0f) {
        return 0;
    }
    if (dbfs >= 0.0f) {
        return 100;
    }
    return (int)((dbfs + 96.0f) * 100.0f / 96.0f);
}

static const char *wifi_state_text(wifi_probe_state_t state)
{
    switch (state) {
    case WIFI_PROBE_UNPROVISIONED:
        return "unprovisioned";
    case WIFI_PROBE_INITIALIZING:
        return "initializing C6...";
    case WIFI_PROBE_CONNECTING:
        return "connecting...";
    case WIFI_PROBE_CONNECTED:
        return "connected";
    case WIFI_PROBE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static uint32_t wifi_state_color(wifi_probe_state_t state)
{
    switch (state) {
    case WIFI_PROBE_CONNECTED:
        return COLOR_GREEN;
    case WIFI_PROBE_CONNECTING:
    case WIFI_PROBE_INITIALIZING:
        return COLOR_AMBER;
    case WIFI_PROBE_FAILED:
        return COLOR_RED;
    default:
        return COLOR_MUTED;
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int w, int h, const lv_font_t *font,
                             lv_event_cb_t cb, void *user_data,
                             lv_obj_t **out_label)
{
    lv_obj_t *btn = ui_theme_card(parent, x, y, w, h);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B2430), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x243040), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);

    lv_obj_t *label = ui_theme_label(btn, text, font, COLOR_TEXT, 0, 0);
    lv_obj_center(label);

    if (out_label != NULL) {
        *out_label = label;
    }
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

static lv_obj_t *make_slider(lv_obj_t *parent, int x, int y, int w, int h,
                             int value, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, h);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    if (cb != NULL) {
        /* Dragging only updates the local preview; the command is sent once on
         * release so a slider drag cannot flood the HA command queue. */
        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, user_data);
        lv_obj_add_event_cb(slider, cb, LV_EVENT_RELEASED, user_data);
    }
    return slider;
}

static lv_obj_t *make_meter(lv_obj_t *parent, int x, int y, int w, int h,
                            uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
    return bar;
}

static void card_clicked_cb(lv_event_t *e)
{
    if (s_ui.detail != NULL) {
        return;
    }
    const int kind = (int)(intptr_t)lv_event_get_user_data(e);
    detail_request_open(kind);
}

static void make_card_clickable(lv_obj_t *card, int kind)
{
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1B2430), LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, card_clicked_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)kind);
}

static void update_header(void)
{
    char buf[64];
    struct tm now;

    if (time_service_get_local(&now)) {
        char date[32];
        strftime(date, sizeof(date), "%a, %d %b %Y", &now);
        snprintf(buf, sizeof(buf), "Card grid  /  %s", date);
    } else {
        snprintf(buf, sizeof(buf), "Card grid  /  clock not synced");
    }

    if (s_ui.header_date != NULL) {
        lv_label_set_text(s_ui.header_date, buf);
    }
}

static void update_clock(void)
{
    struct tm now;
    const bool synced = time_service_get_local(&now);
    char time_text[16];
    char date_text[40];
    char full_date[64];
    char detail_time[16];
    const char *sync_text;

    if (synced) {
        strftime(time_text, sizeof(time_text), "%H:%M", &now);
        strftime(date_text, sizeof(date_text), "%a, %d %b", &now);
        strftime(full_date, sizeof(full_date), "%A, %d %B %Y", &now);
        strftime(detail_time, sizeof(detail_time), "%H:%M:%S", &now);
        sync_text = "NTP: synced";
    } else {
        snprintf(time_text, sizeof(time_text), "--:--");
        snprintf(date_text, sizeof(date_text), "waiting for NTP");
        snprintf(full_date, sizeof(full_date), "waiting for NTP");
        snprintf(detail_time, sizeof(detail_time), "--:--:--");
        sync_text = "NTP: not synced";
    }

    if (s_ui.clock.time != NULL) {
        lv_label_set_text(s_ui.clock.time, time_text);
    }
    if (s_ui.clock.date != NULL) {
        lv_label_set_text(s_ui.clock.date, date_text);
    }
    if (s_ui.clock.sync != NULL) {
        lv_label_set_text(s_ui.clock.sync, sync_text);
        lv_obj_set_style_text_color(s_ui.clock.sync,
                                    lv_color_hex(synced ? COLOR_GREEN : COLOR_AMBER), 0);
    }

    if (s_ui.clock_detail.time != NULL) {
        lv_label_set_text(s_ui.clock_detail.time, detail_time);
    }
    if (s_ui.clock_detail.date != NULL) {
        lv_label_set_text(s_ui.clock_detail.date, full_date);
    }
    if (s_ui.clock_detail.tz != NULL) {
        lv_label_set_text_fmt(s_ui.clock_detail.tz, "TZ: %s", CONFIG_APP_TIMEZONE);
    }
    if (s_ui.clock_detail.sync != NULL) {
        lv_label_set_text(s_ui.clock_detail.sync, sync_text);
        lv_obj_set_style_text_color(s_ui.clock_detail.sync,
                                    lv_color_hex(synced ? COLOR_GREEN : COLOR_AMBER), 0);
    }
}

static const char *ha_state_text(void)
{
    switch (ha_client_state()) {
    case HA_CONNECTED:
        return "HA connected";
    case HA_CONNECTING:
        return "HA connecting";
    default:
        return "HA offline";
    }
}

static bool weather_configured(const char **entity_out)
{
    const char *entity = ha_client_weather_entity();
    const bool configured = entity != NULL && entity[0] != '\0';

    if (entity_out != NULL) {
        *entity_out = configured ? entity : "entity not set";
    }
    return configured;
}

static void update_weather(void)
{
    const char *entity;
    const bool configured = weather_configured(&entity);
    ha_weather_t weather = {0};
    const bool available = configured && ha_client_get_weather(&weather);
    const char *status = ha_state_text();
    char value[16];

    if (available) {
        snprintf(value, sizeof(value), "%.0f°C", weather.temperature_c);
    } else {
        snprintf(value, sizeof(value), "--");
    }
    const char *condition;
    if (available) {
        condition = weather.condition[0] ? weather.condition : "unknown";
    } else if (configured) {
        condition = "Waiting for HA";
    } else {
        condition = "Not configured";
    }

    if (s_ui.weather.value != NULL) {
        lv_label_set_text(s_ui.weather.value, value);
    }
    if (s_ui.weather.condition != NULL) {
        lv_label_set_text(s_ui.weather.condition, condition);
    }
    if (s_ui.weather.entity != NULL) {
        lv_label_set_text(s_ui.weather.entity, entity);
    }
    if (s_ui.weather.status != NULL) {
        lv_label_set_text(s_ui.weather.status, status);
    }

    if (s_ui.weather_detail.value != NULL) {
        lv_label_set_text(s_ui.weather_detail.value, value);
    }
    if (s_ui.weather_detail.entity != NULL) {
        lv_label_set_text_fmt(s_ui.weather_detail.entity, "Entity: %s", entity);
    }
    if (s_ui.weather_detail.condition != NULL) {
        lv_label_set_text(s_ui.weather_detail.condition, condition);
    }
    if (s_ui.weather_detail.status != NULL) {
        lv_label_set_text(s_ui.weather_detail.status, status);
    }
}

static void update_status_network(void)
{
    wifi_probe_snapshot_t w;
    wifi_probe_get(&w);

    const char *state = wifi_state_text(w.state);
    const uint32_t color = wifi_state_color(w.state);

    if (s_ui.status.wifi != NULL) {
        lv_label_set_text(s_ui.status.wifi, state);
        lv_obj_set_style_text_color(s_ui.status.wifi, lv_color_hex(color), 0);
    }
    if (s_ui.status.ip != NULL) {
        lv_label_set_text_fmt(s_ui.status.ip, "IP: %s", w.ip[0] ? w.ip : "---");
    }
    if (s_ui.status.rssi != NULL) {
        lv_label_set_text_fmt(s_ui.status.rssi, "RSSI: %d dBm",
                              w.state == WIFI_PROBE_CONNECTED ? w.rssi : 0);
    }

    if (s_ui.status_detail.wifi != NULL) {
        lv_label_set_text(s_ui.status_detail.wifi, state);
        lv_obj_set_style_text_color(s_ui.status_detail.wifi, lv_color_hex(color), 0);
    }
    if (s_ui.status_detail.ip != NULL) {
        lv_label_set_text_fmt(s_ui.status_detail.ip, "IP: %s", w.ip[0] ? w.ip : "---");
    }
    if (s_ui.status_detail.rssi != NULL) {
        lv_label_set_text_fmt(s_ui.status_detail.rssi, "RSSI: %d dBm",
                              w.state == WIFI_PROBE_CONNECTED ? w.rssi : 0);
    }
    if (s_ui.status_detail.retries != NULL) {
        lv_label_set_text_fmt(s_ui.status_detail.retries, "Retries: %u",
                              (unsigned)w.retries);
    }
    if (s_ui.status_detail.heap != NULL) {
        lv_label_set_text_fmt(s_ui.status_detail.heap,
                              "Free heap: internal %u KB  /  PSRAM %u KB",
                              (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                              (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    }
}

static void update_status_audio(void)
{
    audio_levels_t a = {0};
    const bool running = (audio_analysis_get_levels(&a) == ESP_OK) && a.running;
    int percent = 0;
    char text[24];

    if (running) {
        percent = dbfs_to_percent(a.dbfs_avg);
        snprintf(text, sizeof(text), "%.1f dBFS", a.dbfs_avg);
    } else {
        snprintf(text, sizeof(text), "no signal");
    }

    if (s_ui.status.bar != NULL) {
        lv_bar_set_value(s_ui.status.bar, percent, LV_ANIM_OFF);
    }
    if (s_ui.status.dbfs != NULL) {
        lv_label_set_text(s_ui.status.dbfs, text);
    }

    if (s_ui.status_detail.bar_l != NULL) {
        lv_bar_set_value(s_ui.status_detail.bar_l,
                         running ? dbfs_to_percent(a.dbfs_l) : 0, LV_ANIM_OFF);
    }
    if (s_ui.status_detail.bar_r != NULL) {
        lv_bar_set_value(s_ui.status_detail.bar_r,
                         running ? dbfs_to_percent(a.dbfs_r) : 0, LV_ANIM_OFF);
    }
    if (s_ui.status_detail.dbfs_l != NULL) {
        if (running) {
            lv_label_set_text_fmt(s_ui.status_detail.dbfs_l, "%.1f dBFS", a.dbfs_l);
        } else {
            lv_label_set_text(s_ui.status_detail.dbfs_l, "-- dBFS");
        }
    }
    if (s_ui.status_detail.dbfs_r != NULL) {
        if (running) {
            lv_label_set_text_fmt(s_ui.status_detail.dbfs_r, "%.1f dBFS", a.dbfs_r);
        } else {
            lv_label_set_text(s_ui.status_detail.dbfs_r, "-- dBFS");
        }
    }
    if (s_ui.status_detail.dbfs_avg != NULL) {
        if (running) {
            lv_label_set_text_fmt(s_ui.status_detail.dbfs_avg, "%.1f dBFS", a.dbfs_avg);
        } else {
            lv_label_set_text(s_ui.status_detail.dbfs_avg, "-- dBFS");
        }
    }
    if (s_ui.status_detail.dbfs_peak != NULL) {
        if (running) {
            lv_label_set_text_fmt(s_ui.status_detail.dbfs_peak, "%.1f dBFS", a.peak_dbfs);
        } else {
            lv_label_set_text(s_ui.status_detail.dbfs_peak, "-- dBFS");
        }
    }
}

static bool light_entity_configured(const light_ui_t *light)
{
    const char *entity = ha_client_light_entity(light->index);
    return entity != NULL && entity[0] != '\0';
}

static const char *light_state_text(const light_ui_t *light)
{
    if (!light_entity_configured(light) || !ha_client_configured()) {
        return "NOT CONFIGURED";
    }

    const ha_state_t state = ha_client_state();
    if (state == HA_OFFLINE) {
        return "OFFLINE";
    }
    if (light->pending) {
        return "PENDING";
    }
    if (light->remote_valid) {
        return light->remote_on ? "ON" : "OFF";
    }
    if (state == HA_CONNECTED) {
        return "SYNCING";
    }
    return "CONNECTING";
}

static uint32_t light_state_color(const char *state_text)
{
    if (strcmp(state_text, "ON") == 0) {
        return COLOR_GREEN;
    }
    if (strcmp(state_text, "OFF") == 0) {
        return COLOR_MUTED;
    }
    return COLOR_AMBER;
}

static const char *light_hint_text(const light_ui_t *light)
{
    if (!ha_client_configured()) {
        return "HA offline - local preview";
    }
    if (!light_entity_configured(light)) {
        return "entity not configured";
    }

    const ha_state_t state = ha_client_state();
    if (state == HA_OFFLINE) {
        return "HA offline - local preview";
    }
    if (light->pending) {
        return "sent - waiting for HA";
    }
    if (state == HA_CONNECTED) {
        return light->remote_valid ? "HA connected" : "HA syncing";
    }
    return "HA connecting - local preview";
}

static void light_refresh(light_ui_t *light)
{
    const char *state_text = light_state_text(light);
    const uint32_t state_color = light_state_color(state_text);
    const char *hint = light_hint_text(light);

    if (light->state != NULL) {
        lv_label_set_text(light->state, state_text);
        lv_obj_set_style_text_color(light->state, lv_color_hex(state_color), 0);
    }
    if (light->power_label != NULL) {
        lv_label_set_text(light->power_label, light->on ? "TURN OFF" : "TURN ON");
    }
    if (light->brightness_label != NULL) {
        lv_label_set_text_fmt(light->brightness_label, "%d%%", light->brightness);
    }
    if (light->hint != NULL) {
        lv_label_set_text(light->hint, hint);
    }

    if (light->detail_state != NULL) {
        lv_label_set_text(light->detail_state, state_text);
        lv_obj_set_style_text_color(light->detail_state, lv_color_hex(state_color), 0);
    }
    if (light->detail_power_label != NULL) {
        lv_label_set_text(light->detail_power_label, light->on ? "TURN OFF" : "TURN ON");
    }
    if (light->detail_brightness_label != NULL) {
        lv_label_set_text_fmt(light->detail_brightness_label, "%d%%", light->brightness);
    }
    if (light->detail_hint != NULL) {
        lv_label_set_text(light->detail_hint, hint);
    }

    /* Programmatic slider updates would otherwise re-enter the value-changed
     * callback and queue a second HA command. Do not overwrite a slider the
     * user is currently dragging. */
    light->applying = true;
    if (light->slider != NULL &&
        !lv_obj_has_state(light->slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(light->slider, light->brightness, LV_ANIM_OFF);
    }
    if (light->detail_slider != NULL &&
        !lv_obj_has_state(light->detail_slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(light->detail_slider, light->brightness, LV_ANIM_OFF);
    }
    light->applying = false;
}

static void light_pull(light_ui_t *light)
{
    ha_light_state_t state;
    const uint32_t now = lv_tick_get();

    if (ha_client_get_light(light->index, &state) && state.available) {
        light->remote_valid = true;
        light->remote_on = state.on;

        if (light->pending) {
            const bool matches = (state.on == light->on) &&
                                 (!state.on || state.brightness == light->brightness);
            const bool expired = (int32_t)(now - light->pending_until_ms) >= 0;
            if (matches || expired) {
                light->pending = false;
            }
        }

        const bool dragging =
            (light->slider != NULL &&
             lv_obj_has_state(light->slider, LV_STATE_PRESSED)) ||
            (light->detail_slider != NULL &&
             lv_obj_has_state(light->detail_slider, LV_STATE_PRESSED));
        if (!light->pending && !dragging) {
            light->on = state.on;
            light->brightness = state.brightness;
        }
    } else {
        light->remote_valid = false;
        if (light->pending && (int32_t)(now - light->pending_until_ms) >= 0) {
            light->pending = false;
        }
    }
}

static void light_command_queued(light_ui_t *light, esp_err_t err)
{
    light->pending = (err == ESP_OK);
    if (light->pending) {
        light->pending_until_ms = lv_tick_get() + 5000;
    }
}

static void light_power_cb(lv_event_t *e)
{
    light_ui_t *light = (light_ui_t *)lv_event_get_user_data(e);
    if (light == NULL) {
        return;
    }

    light->on = !light->on;
    /* Queued by the HA client; this call never performs HTTP on the LVGL task. */
    light_command_queued(light, ha_client_set_light(light->index, light->on,
                                                     light->brightness));
    light_refresh(light);
}

static void light_slider_cb(lv_event_t *e)
{
    light_ui_t *light = (light_ui_t *)lv_event_get_user_data(e);
    if (light == NULL || light->applying) {
        return;
    }

    lv_obj_t *slider = lv_event_get_target(e);
    light->brightness = (int)lv_slider_get_value(slider);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        light_command_queued(light, ha_client_set_light(light->index, light->on,
                                                         light->brightness));
    } else {
        /* VALUE_CHANGED is preview only: no command per drag tick. */
        light->pending = false;
    }
    light_refresh(light);
}

static void update_lights(void)
{
    for (int i = 0; i < HA_LIGHT_COUNT; i++) {
        light_pull(&s_lights[i]);
        light_refresh(&s_lights[i]);
    }
}

static void diagnostics_clicked_cb(lv_event_t *e)
{
    (void)e;
    const esp_err_t err = app_manager_request("diagnostics");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "diagnostics request failed: %s", esp_err_to_name(err));
    }
}

static void back_clicked_cb(lv_event_t *e)
{
    (void)e;
    detail_request_close();
}

static void add_detail_header(lv_obj_t *detail, const char *title)
{
    make_button(detail, "BACK", 38, 20, 96, 40, &lv_font_montserrat_14,
                back_clicked_cb, NULL, NULL);
    ui_theme_label(detail, title, &lv_font_montserrat_28, COLOR_TEXT, 150, 24);
}

static void build_clock_detail(lv_obj_t *detail)
{
    lv_obj_t *card = ui_theme_card(detail, 112, 110, 800, 390);
    ui_theme_label(card, "LOCAL TIME", &lv_font_montserrat_14, COLOR_ACCENT, 40, 20);
    s_ui.clock_detail.time = ui_theme_label(card, "--:--:--", &lv_font_montserrat_48,
                                            COLOR_TEXT, 40, 56);
    s_ui.clock_detail.date = ui_theme_label(card, "waiting for NTP",
                                            &lv_font_montserrat_28, COLOR_TEXT, 40, 140);
    s_ui.clock_detail.tz = ui_theme_label(card, "TZ: " CONFIG_APP_TIMEZONE,
                                          &lv_font_montserrat_20, COLOR_MUTED, 40, 200);
    s_ui.clock_detail.sync = ui_theme_label(card, "NTP: not synced",
                                            &lv_font_montserrat_20, COLOR_AMBER, 40, 236);
    ui_theme_label(card,
                   "SNTP polls pool.ntp.org. The dashboard shows 'not synced' until\n"
                   "the system clock is valid; it never displays 1970.",
                   &lv_font_montserrat_14, COLOR_DIM, 40, 300);
}

static void build_weather_detail(lv_obj_t *detail)
{
    lv_obj_t *card = ui_theme_card(detail, 112, 110, 800, 390);
    ui_theme_label(card, "WEATHER  /  HOME ASSISTANT", &lv_font_montserrat_20,
                   COLOR_ACCENT, 40, 20);
    s_ui.weather_detail.value = ui_theme_label(card, "--", &lv_font_montserrat_48,
                                               COLOR_TEXT, 40, 60);
    s_ui.weather_detail.condition = ui_theme_label(card, "Not configured",
                                                   &lv_font_montserrat_28,
                                                   COLOR_TEXT, 40, 150);
    s_ui.weather_detail.entity = ui_theme_label(card, "Entity: entity not set",
                                                &lv_font_montserrat_20,
                                                COLOR_MUTED, 40, 210);
    lv_obj_set_width(s_ui.weather_detail.entity, 720);
    lv_label_set_long_mode(s_ui.weather_detail.entity, LV_LABEL_LONG_DOT);
    s_ui.weather_detail.status = ui_theme_label(card, "HA offline",
                                                &lv_font_montserrat_20,
                                                COLOR_AMBER, 40, 250);
    ui_theme_label(card,
                   "Weather data arrives with the HA client (M7b).\n"
                   "M7a shows only the configured entity and connection state.",
                   &lv_font_montserrat_14, COLOR_DIM, 40, 310);
}

static void build_status_detail(lv_obj_t *detail)
{
    lv_obj_t *net = ui_theme_card(detail, 38, 96, 460, 430);
    ui_theme_label(net, "NETWORK", &lv_font_montserrat_20, COLOR_ACCENT, 24, 18);
    s_ui.status_detail.wifi = ui_theme_label(net, "initializing...",
                                             &lv_font_montserrat_28, COLOR_AMBER, 24, 56);
    s_ui.status_detail.ip = ui_theme_label(net, "IP: ---", &lv_font_montserrat_20,
                                           COLOR_TEXT, 24, 104);
    s_ui.status_detail.rssi = ui_theme_label(net, "RSSI: 0 dBm", &lv_font_montserrat_20,
                                             COLOR_TEXT, 24, 136);
    s_ui.status_detail.retries = ui_theme_label(net, "Retries: 0",
                                                &lv_font_montserrat_14,
                                                COLOR_MUTED, 24, 168);
    s_ui.status_detail.heap = ui_theme_label(net, "Free heap: ---",
                                             &lv_font_montserrat_14,
                                             COLOR_MUTED, 24, 196);
    lv_obj_set_width(s_ui.status_detail.heap, 410);
    lv_label_set_long_mode(s_ui.status_detail.heap, LV_LABEL_LONG_WRAP);
    ui_theme_label(net,
                   "Wi-Fi comes from the ESP32-C6 over SDIO.\n"
                   "RSSI is sampled every 2 s by wifi_probe.",
                   &lv_font_montserrat_14, COLOR_DIM, 24, 250);

    lv_obj_t *audio = ui_theme_card(detail, 526, 96, 460, 430);
    ui_theme_label(audio, "AUDIO  /  ES7210", &lv_font_montserrat_20,
                   COLOR_ACCENT, 24, 18);

    ui_theme_label(audio, "L", &lv_font_montserrat_14, COLOR_TEXT, 24, 56);
    s_ui.status_detail.bar_l = make_meter(audio, 52, 58, 260, 16, COLOR_ACCENT);
    s_ui.status_detail.dbfs_l = ui_theme_label(audio, "-- dBFS",
                                               &lv_font_montserrat_14,
                                               COLOR_TEXT, 330, 54);

    ui_theme_label(audio, "R", &lv_font_montserrat_14, COLOR_TEXT, 24, 96);
    s_ui.status_detail.bar_r = make_meter(audio, 52, 98, 260, 16, 0xE879F9);
    s_ui.status_detail.dbfs_r = ui_theme_label(audio, "-- dBFS",
                                               &lv_font_montserrat_14,
                                               COLOR_TEXT, 330, 94);

    ui_theme_label(audio, "AVG", &lv_font_montserrat_14, COLOR_MUTED, 24, 150);
    s_ui.status_detail.dbfs_avg = ui_theme_label(audio, "-- dBFS",
                                                 &lv_font_montserrat_28,
                                                 COLOR_GREEN, 24, 174);
    ui_theme_label(audio, "PEAK", &lv_font_montserrat_14, COLOR_MUTED, 230, 150);
    s_ui.status_detail.dbfs_peak = ui_theme_label(audio, "-- dBFS",
                                                  &lv_font_montserrat_28,
                                                  COLOR_AMBER, 230, 174);

    make_button(audio, "OPEN DIAGNOSTICS", 24, 300, 300, 60,
                &lv_font_montserrat_20, diagnostics_clicked_cb, NULL, NULL);
    ui_theme_label(audio, "Relative dBFS only; not calibrated dB SPL.",
                   &lv_font_montserrat_14, COLOR_DIM, 24, 380);
}

static void build_light_detail(lv_obj_t *detail, int index)
{
    light_ui_t *light = &s_lights[index];

    lv_obj_t *card = ui_theme_card(detail, 212, 96, 600, 440);
    ui_theme_label(card, "HOME ASSISTANT LIGHT", &lv_font_montserrat_14,
                   COLOR_ACCENT, 30, 18);

    const char *entity = ha_client_light_entity(index);
    lv_obj_t *entity_label = ui_theme_label(card,
                                            entity[0] ? entity : "entity not set",
                                            &lv_font_montserrat_20,
                                            COLOR_TEXT, 30, 44);
    lv_obj_set_width(entity_label, 540);
    lv_label_set_long_mode(entity_label, LV_LABEL_LONG_DOT);

    ui_theme_label(card, "STATE", &lv_font_montserrat_14, COLOR_DIM, 30, 84);
    light->detail_state = ui_theme_label(card, "NOT CONFIGURED",
                                         &lv_font_montserrat_28, COLOR_AMBER, 30, 106);

    make_button(card, "TURN ON", 30, 160, 260, 70, &lv_font_montserrat_28,
                light_power_cb, light, &light->detail_power_label);

    ui_theme_label(card, "BRIGHTNESS", &lv_font_montserrat_14, COLOR_DIM, 30, 260);
    light->detail_brightness_label = ui_theme_label(card, "100%",
                                                    &lv_font_montserrat_28,
                                                    COLOR_TEXT, 460, 250);
    lv_obj_set_width(light->detail_brightness_label, 110);
    lv_obj_set_style_text_align(light->detail_brightness_label, LV_TEXT_ALIGN_RIGHT, 0);

    light->detail_slider = make_slider(card, 30, 300, 540, 24, light->brightness,
                                       light_slider_cb, light);
    light->detail_hint = ui_theme_label(card, "HA offline - local preview",
                                        &lv_font_montserrat_14,
                                        COLOR_MUTED, 30, 350);
    ui_theme_label(card,
                   "M7a stub: controls update this local preview only until the\n"
                   "Home Assistant client lands in M7b.",
                   &lv_font_montserrat_14, COLOR_DIM, 30, 380);
}

static void detail_close_now(void)
{
    if (s_ui.detail == NULL) {
        return;
    }

    lv_obj_t *detail = s_ui.detail;
    s_ui.detail = NULL;
    s_ui.detail_kind = -1;

    memset(&s_ui.clock_detail, 0, sizeof(s_ui.clock_detail));
    memset(&s_ui.weather_detail, 0, sizeof(s_ui.weather_detail));
    memset(&s_ui.status_detail, 0, sizeof(s_ui.status_detail));
    for (int i = 0; i < HA_LIGHT_COUNT; i++) {
        s_lights[i].detail_state = NULL;
        s_lights[i].detail_power_label = NULL;
        s_lights[i].detail_slider = NULL;
        s_lights[i].detail_brightness_label = NULL;
        s_lights[i].detail_hint = NULL;
    }

    lv_obj_delete(detail);
    if (s_ui.grid != NULL) {
        lv_obj_remove_flag(s_ui.grid, LV_OBJ_FLAG_HIDDEN);
    }
}

static void detail_open_now(int kind)
{
    if (s_ui.dash == NULL || s_ui.grid == NULL || s_ui.detail != NULL) {
        return;
    }

    lv_obj_add_flag(s_ui.grid, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *detail = lv_obj_create(s_ui.dash);
    lv_obj_set_pos(detail, 0, 0);
    lv_obj_set_size(detail, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(detail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(detail, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(detail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(detail, 0, 0);
    lv_obj_set_style_radius(detail, 0, 0);
    lv_obj_set_style_pad_all(detail, 0, 0);

    s_ui.detail = detail;
    s_ui.detail_kind = kind;

    if (kind == CARD_CLOCK) {
        add_detail_header(detail, "CLOCK");
        build_clock_detail(detail);
        update_clock();
    } else if (kind == CARD_WEATHER) {
        add_detail_header(detail, "WEATHER");
        build_weather_detail(detail);
        update_weather();
    } else if (kind == CARD_STATUS) {
        add_detail_header(detail, "STATUS");
        build_status_detail(detail);
        update_status_network();
        update_status_audio();
    } else if (kind >= CARD_LIGHT_0 && kind < CARD_LIGHT_0 + HA_LIGHT_COUNT) {
        const int index = kind - CARD_LIGHT_0;
        char title[16];
        snprintf(title, sizeof(title), "LIGHT %d", index);
        add_detail_header(detail, title);
        build_light_detail(detail, index);
        light_refresh(&s_lights[index]);
    }
}

static void detail_async_cb(void *arg)
{
    (void)arg;
    if (!s_async_pending) {
        return;
    }
    s_async_pending = false;

    if (s_close_requested) {
        s_close_requested = false;
        detail_close_now();
        return;
    }
    if (s_open_requested) {
        s_open_requested = false;
        detail_open_now(s_requested_kind);
    }
}

static void request_detail_async(void)
{
    if (lv_async_call(detail_async_cb, NULL) != LV_RESULT_OK) {
        s_async_pending = false;
        s_open_requested = false;
        s_close_requested = false;
        ESP_LOGW(TAG, "could not defer detail transition");
    }
}

static void detail_request_open(int kind)
{
    if (s_ui.detail != NULL || s_async_pending) {
        return;
    }
    s_open_requested = true;
    s_close_requested = false;
    s_requested_kind = kind;
    s_async_pending = true;
    request_detail_async();
}

static void detail_request_close(void)
{
    if (s_ui.detail == NULL || s_async_pending) {
        return;
    }
    s_close_requested = true;
    s_open_requested = false;
    s_async_pending = true;
    request_detail_async();
}

static void build_clock_card(lv_obj_t *grid)
{
    lv_obj_t *card = ui_theme_card(grid, 38, 96, 300, 190);
    make_card_clickable(card, CARD_CLOCK);

    ui_theme_label(card, "CLOCK", &lv_font_montserrat_14, COLOR_ACCENT, 18, 14);
    s_ui.clock.time = ui_theme_label(card, "--:--", &lv_font_montserrat_28,
                                     COLOR_TEXT, 18, 42);
    s_ui.clock.date = ui_theme_label(card, "waiting for NTP",
                                     &lv_font_montserrat_14, COLOR_MUTED, 18, 92);
    s_ui.clock.sync = ui_theme_label(card, "NTP: not synced",
                                     &lv_font_montserrat_14, COLOR_AMBER, 18, 118);
    ui_theme_label(card, "Tap for details", &lv_font_montserrat_14,
                   COLOR_DIM, 18, 152);
}

static void build_weather_card(lv_obj_t *grid)
{
    lv_obj_t *card = ui_theme_card(grid, 362, 96, 300, 190);
    make_card_clickable(card, CARD_WEATHER);

    ui_theme_label(card, "WEATHER", &lv_font_montserrat_14, COLOR_ACCENT, 18, 14);
    s_ui.weather.value = ui_theme_label(card, "--", &lv_font_montserrat_28,
                                        COLOR_TEXT, 18, 42);
    s_ui.weather.condition = ui_theme_label(card, "Not configured",
                                            &lv_font_montserrat_14,
                                            COLOR_MUTED, 18, 88);
    s_ui.weather.entity = ui_theme_label(card, "entity not set",
                                         &lv_font_montserrat_14,
                                         COLOR_DIM, 18, 112);
    lv_obj_set_width(s_ui.weather.entity, 264);
    lv_label_set_long_mode(s_ui.weather.entity, LV_LABEL_LONG_DOT);
    s_ui.weather.status = ui_theme_label(card, "HA offline",
                                         &lv_font_montserrat_14,
                                         COLOR_DIM, 18, 136);
}

static void build_status_card(lv_obj_t *grid)
{
    lv_obj_t *card = ui_theme_card(grid, 686, 96, 300, 190);
    make_card_clickable(card, CARD_STATUS);

    ui_theme_label(card, "STATUS", &lv_font_montserrat_14, COLOR_ACCENT, 18, 12);
    s_ui.status.wifi = ui_theme_label(card, "initializing...",
                                      &lv_font_montserrat_20, COLOR_AMBER, 18, 36);
    s_ui.status.ip = ui_theme_label(card, "IP: ---", &lv_font_montserrat_14,
                                    COLOR_TEXT, 18, 64);
    s_ui.status.rssi = ui_theme_label(card, "RSSI: 0 dBm", &lv_font_montserrat_14,
                                      COLOR_MUTED, 18, 86);

    ui_theme_label(card, "MIC", &lv_font_montserrat_14, COLOR_MUTED, 18, 106);
    s_ui.status.bar = make_meter(card, 54, 109, 168, 10, COLOR_ACCENT);
    s_ui.status.dbfs = ui_theme_label(card, "no signal", &lv_font_montserrat_14,
                                      COLOR_TEXT, 18, 126);

    make_button(card, "DIAGNOSTICS", 18, 146, 264, 34, &lv_font_montserrat_14,
                diagnostics_clicked_cb, NULL, NULL);
}

static void build_light_card(lv_obj_t *grid, int index)
{
    const int x = 38 + index * 241;
    light_ui_t *light = &s_lights[index];

    light->index = index;
    light->on = false;
    light->brightness = 100;
    light->remote_valid = false;
    light->applying = false;

    lv_obj_t *card = ui_theme_card(grid, x, 310, 225, 230);
    make_card_clickable(card, CARD_LIGHT_0 + index);

    char title[16];
    snprintf(title, sizeof(title), "LIGHT %d", index);
    ui_theme_label(card, title, &lv_font_montserrat_14, COLOR_ACCENT, 14, 12);

    const char *entity = ha_client_light_entity(index);
    lv_obj_t *entity_label = ui_theme_label(card,
                                            entity[0] ? entity : "entity not set",
                                            &lv_font_montserrat_14,
                                            COLOR_MUTED, 14, 34);
    lv_obj_set_width(entity_label, 197);
    lv_label_set_long_mode(entity_label, LV_LABEL_LONG_DOT);

    light->state = ui_theme_label(card, "NOT CONFIGURED", &lv_font_montserrat_20,
                                  COLOR_AMBER, 14, 58);
    make_button(card, "TURN ON", 14, 90, 197, 40, &lv_font_montserrat_20,
                light_power_cb, light, &light->power_label);

    ui_theme_label(card, "BRIGHTNESS", &lv_font_montserrat_14, COLOR_DIM, 14, 140);
    light->brightness_label = ui_theme_label(card, "100%", &lv_font_montserrat_14,
                                             COLOR_TEXT, 170, 140);
    lv_obj_set_width(light->brightness_label, 41);
    lv_obj_set_style_text_align(light->brightness_label, LV_TEXT_ALIGN_RIGHT, 0);

    light->slider = make_slider(card, 14, 162, 197, 16, light->brightness,
                                light_slider_cb, light);
    light->hint = ui_theme_label(card, "HA offline - local preview",
                                 &lv_font_montserrat_14, COLOR_DIM, 14, 190);
    lv_obj_set_width(light->hint, 197);
    lv_label_set_long_mode(light->hint, LV_LABEL_LONG_DOT);
}

static void dashboard_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    const uint32_t tick = ++s_ticks;
    update_status_audio();

    if ((tick % HA_TICK_DIVIDER) == 0) {
        update_weather();
        update_lights();
    }
    if ((tick % SLOW_TICK_DIVIDER) == 0) {
        update_header();
        update_clock();
        update_status_network();
    }
}

esp_err_t dashboard_mount(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bsp_display_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }

    memset(&s_ui, 0, sizeof(s_ui));
    memset(s_lights, 0, sizeof(s_lights));
    s_ui.detail_kind = -1;
    s_ticks = 0;
    s_async_pending = false;
    s_open_requested = false;
    s_close_requested = false;
    s_requested_kind = 0;

    s_ui.dash = lv_obj_create(parent);
    lv_obj_set_pos(s_ui.dash, 0, 0);
    lv_obj_set_size(s_ui.dash, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(s_ui.dash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_ui.dash, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.dash, 0, 0);
    lv_obj_set_style_radius(s_ui.dash, 0, 0);
    lv_obj_set_style_pad_all(s_ui.dash, 0, 0);

    s_ui.grid = lv_obj_create(s_ui.dash);
    lv_obj_set_pos(s_ui.grid, 0, 0);
    lv_obj_set_size(s_ui.grid, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(s_ui.grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_ui.grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.grid, 0, 0);
    lv_obj_set_style_radius(s_ui.grid, 0, 0);
    lv_obj_set_style_pad_all(s_ui.grid, 0, 0);

    ui_theme_label(s_ui.grid, "DASHBOARD", &lv_font_montserrat_28,
                   COLOR_TEXT, 38, 24);
    s_ui.header_date = ui_theme_label(s_ui.grid, "Card grid  /  clock not synced",
                                      &lv_font_montserrat_14, COLOR_MUTED, 40, 62);

    build_clock_card(s_ui.grid);
    build_weather_card(s_ui.grid);
    build_status_card(s_ui.grid);
    for (int i = 0; i < HA_LIGHT_COUNT; i++) {
        build_light_card(s_ui.grid, i);
    }

    update_header();
    update_clock();
    update_weather();
    update_status_network();
    update_status_audio();
    update_lights();

    s_timer = lv_timer_create(dashboard_timer_cb, TIMER_PERIOD_MS, NULL);
    if (s_timer == NULL) {
        bsp_display_unlock();
        return ESP_ERR_NO_MEM;
    }

    bsp_display_unlock();
    ESP_LOGI(TAG, "dashboard mounted (HA %s)",
             ha_client_configured() ? "configured" : "offline");
    return ESP_OK;
}

void dashboard_unmount(void)
{
    if (!bsp_display_lock(-1)) {
        return;
    }

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    s_async_pending = false;
    s_open_requested = false;
    s_close_requested = false;
    s_requested_kind = 0;
    s_ticks = 0;
    memset(&s_ui, 0, sizeof(s_ui));
    memset(s_lights, 0, sizeof(s_lights));

    bsp_display_unlock();
}

const app_desc_t dashboard_app = {
    .id = "dashboard",
    .title = "Dashboard",
    .description = "Clock, weather, status and four light cards.",
    .mount = dashboard_mount,
    .unmount = dashboard_unmount,
};
