#include "ui_probe.h"

#include <stdio.h>

#include "audio_analysis.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "temperature_service.h"
#include "wifi_probe.h"

static const char *TAG = "ui_probe";

static lv_display_t *s_disp;
static lv_obj_t *s_audio_status;
static lv_obj_t *s_bar_l;
static lv_obj_t *s_bar_r;
static lv_obj_t *s_label_l;
static lv_obj_t *s_label_r;
static lv_obj_t *s_label_avg;
static lv_obj_t *s_label_peak;
static lv_obj_t *s_wifi_state;
static lv_obj_t *s_wifi_ip;
static lv_obj_t *s_wifi_rssi;
static lv_obj_t *s_temp_label;
static lv_obj_t *s_touch_label;
static lv_obj_t *s_fps_label;
static lv_obj_t *s_touch_overlay;
static lv_obj_t *s_touch_dot;
static lv_timer_t *s_timer;

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x151B23), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3542), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    return card;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

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
        return 0x4ADE80;
    case WIFI_PROBE_CONNECTING:
    case WIFI_PROBE_INITIALIZING:
        return 0xFBBF24;
    case WIFI_PROBE_FAILED:
        return 0xF87171;
    default:
        return 0x94A3B8;
    }
}

static void touch_event_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "touch x=%d y=%d", (int)p.x, (int)p.y);
    }
    lv_label_set_text_fmt(s_touch_label, "x = %d   y = %d", (int)p.x, (int)p.y);

    if (s_touch_dot != NULL) {
        lv_obj_set_pos(s_touch_dot, (int)p.x - 9, (int)p.y - 9);
        lv_obj_remove_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_touch_dot);
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    audio_levels_t a;
    audio_analysis_get_levels(&a);
    if (a.running) {
        lv_label_set_text(s_audio_status, "ES7210 running  |  16 kHz  |  stereo 16-bit");
        lv_label_set_text_fmt(s_label_l, "%.1f dBFS", a.dbfs_l);
        lv_label_set_text_fmt(s_label_r, "%.1f dBFS", a.dbfs_r);
        lv_label_set_text_fmt(s_label_avg, "%.1f dBFS", a.dbfs_avg);
        lv_label_set_text_fmt(s_label_peak, "%.1f dBFS", a.peak_dbfs);
        lv_bar_set_value(s_bar_l, dbfs_to_percent(a.dbfs_l), LV_ANIM_OFF);
        lv_bar_set_value(s_bar_r, dbfs_to_percent(a.dbfs_r), LV_ANIM_OFF);
    } else {
        lv_label_set_text(s_audio_status, a.error ? "ES7210 init/read failed" : "ES7210 starting...");
        lv_bar_set_value(s_bar_l, 0, LV_ANIM_OFF);
        lv_bar_set_value(s_bar_r, 0, LV_ANIM_OFF);
    }

    wifi_probe_snapshot_t w;
    wifi_probe_get(&w);
    lv_label_set_text(s_wifi_state, wifi_state_text(w.state));
    lv_obj_set_style_text_color(s_wifi_state, lv_color_hex(wifi_state_color(w.state)), 0);
    lv_label_set_text_fmt(s_wifi_ip, "IP: %s", w.ip[0] ? w.ip : "---");
    lv_label_set_text_fmt(s_wifi_rssi, "RSSI: %d dBm   retries: %u",
                          w.state == WIFI_PROBE_CONNECTED ? w.rssi : 0,
                          (unsigned)w.retries);

    float temp_c = 0.0f;
    if (temperature_service_read(&temp_c)) {
        lv_label_set_text_fmt(s_temp_label, "P4 die: %.1f C", (double)temp_c);
    } else {
        lv_label_set_text(s_temp_label, "P4 die: --");
    }

    uint32_t fps = 0;
    if (esp_lv_adapter_get_fps(s_disp, &fps) == ESP_OK) {
        lv_label_set_text_fmt(s_fps_label, "%u fps", (unsigned)fps);
    } else {
        lv_label_set_text(s_fps_label, "-- fps");
    }

    static uint32_t tick_count;
    if ((++tick_count % 100) == 0) {
        ESP_LOGI(TAG, "fps=%u internal=%u psram=%u",
                 (unsigned)fps,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

esp_err_t ui_probe_mount(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_disp = lv_display_get_default();
    if (s_disp == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bsp_display_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *scr = parent;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B0F14), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    make_label(scr, "DIAGNOSTICS", &lv_font_montserrat_28, 0xE2E8F0, 32, 24);
    make_label(scr, "Live bring-up diagnostics  /  ESP32-P4  /  1024 x 600  /  LVGL 9.5",
               &lv_font_montserrat_14, 0x94A3B8, 34, 62);

    lv_obj_t *left = make_card(scr, 32, 108, 600, 372);
    make_label(left, "AUDIO  /  ES7210 DUAL MIC", &lv_font_montserrat_20, 0x22D3EE, 20, 14);
    s_audio_status = make_label(left, "ES7210 starting...", &lv_font_montserrat_14, 0x94A3B8, 20, 44);

    make_label(left, "L", &lv_font_montserrat_20, 0xE2E8F0, 20, 82);
    s_bar_l = lv_bar_create(left);
    lv_obj_set_pos(s_bar_l, 52, 84);
    lv_obj_set_size(s_bar_l, 420, 24);
    lv_bar_set_range(s_bar_l, 0, 100);
    lv_obj_set_style_bg_color(s_bar_l, lv_color_hex(0x0B0F14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar_l, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_l, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_l, lv_color_hex(0x22D3EE), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_l, 6, LV_PART_INDICATOR);
    s_label_l = make_label(left, "-- dBFS", &lv_font_montserrat_20, 0xE2E8F0, 486, 80);

    make_label(left, "R", &lv_font_montserrat_20, 0xE2E8F0, 20, 132);
    s_bar_r = lv_bar_create(left);
    lv_obj_set_pos(s_bar_r, 52, 134);
    lv_obj_set_size(s_bar_r, 420, 24);
    lv_bar_set_range(s_bar_r, 0, 100);
    lv_obj_set_style_bg_color(s_bar_r, lv_color_hex(0x0B0F14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar_r, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_r, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_r, lv_color_hex(0xE879F9), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_r, 6, LV_PART_INDICATOR);
    s_label_r = make_label(left, "-- dBFS", &lv_font_montserrat_20, 0xE2E8F0, 486, 130);

    make_label(left, "AVG", &lv_font_montserrat_14, 0x94A3B8, 20, 196);
    s_label_avg = make_label(left, "-- dBFS", &lv_font_montserrat_28, 0x4ADE80, 20, 218);
    make_label(left, "PEAK", &lv_font_montserrat_14, 0x94A3B8, 300, 196);
    s_label_peak = make_label(left, "-- dBFS", &lv_font_montserrat_28, 0xFBBF24, 300, 218);

    make_label(left,
               "Relative measurement only. dBFS is digital headroom;\n"
               "it is not calibrated dB SPL. PGA gain changes the reading.",
               &lv_font_montserrat_14, 0x64748B, 20, 292);

    lv_obj_t *right = make_card(scr, 656, 108, 336, 372);
    make_label(right, "NETWORK  /  ESP32-C6", &lv_font_montserrat_20, 0x22D3EE, 16, 14);
    s_wifi_state = make_label(right, "initializing...", &lv_font_montserrat_20, 0xFBBF24, 16, 48);
    s_wifi_ip = make_label(right, "IP: ---", &lv_font_montserrat_14, 0xE2E8F0, 16, 84);
    s_wifi_rssi = make_label(right, "RSSI: 0 dBm   retries: 0", &lv_font_montserrat_14, 0x94A3B8, 16, 108);
    s_temp_label = make_label(right, "P4 die: --", &lv_font_montserrat_14, 0xFBBF24, 16, 132);

    make_label(right, "TOUCH  /  GT911", &lv_font_montserrat_20, 0x22D3EE, 16, 156);
    s_touch_label = make_label(right, "touch anywhere", &lv_font_montserrat_20, 0xE2E8F0, 16, 190);

    make_label(right, "DISPLAY  /  EK79007", &lv_font_montserrat_20, 0x22D3EE, 16, 240);
    s_fps_label = make_label(right, "-- fps", &lv_font_montserrat_20, 0x4ADE80, 16, 274);
    make_label(right, "MIPI-DSI 2-lane  |  60 Hz panel", &lv_font_montserrat_14, 0x64748B, 16, 310);

    lv_obj_t *footer = make_card(scr, 32, 500, 960, 68);
    make_label(footer,
               "Touch the panel to verify GT911 coordinates.  Speak or tap near the mics to verify ES7210.\n"
               "Wi-Fi comes from the C6 over SDIO; configure SSID/password in menuconfig.",
               &lv_font_montserrat_14, 0x94A3B8, 20, 12);

    /* Full-screen transparent input layer, kept above the cards. */
    s_touch_overlay = lv_obj_create(scr);
    lv_obj_set_pos(s_touch_overlay, 0, 0);
    lv_obj_set_size(s_touch_overlay, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(s_touch_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_touch_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_touch_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_touch_overlay, 0, 0);
    lv_obj_add_event_cb(s_touch_overlay, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_touch_overlay, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_move_foreground(s_touch_overlay);

    /* One persistent marker. Do not delete/recreate an LVGL object inside a
     * touch event callback; that can corrupt the event/draw lists and hang the
     * UI after a number of touches. */
    s_touch_dot = lv_obj_create(s_touch_overlay);
    lv_obj_remove_flag(s_touch_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_touch_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_touch_dot, 18, 18);
    lv_obj_set_style_radius(s_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(0x22D3EE), 0);
    lv_obj_set_style_bg_opa(s_touch_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_touch_dot, 0, 0);

    s_timer = lv_timer_create(ui_timer_cb, 50, NULL);
    if (s_timer == NULL) {
        bsp_display_unlock();
        return ESP_ERR_NO_MEM;
    }

    bsp_display_unlock();
    ESP_LOGI(TAG, "UI ready");
    return ESP_OK;
}

void ui_probe_unmount(void)
{
    if (!bsp_display_lock(-1)) {
        return;
    }

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    s_disp = NULL;
    s_audio_status = NULL;
    s_bar_l = NULL;
    s_bar_r = NULL;
    s_label_l = NULL;
    s_label_r = NULL;
    s_label_avg = NULL;
    s_label_peak = NULL;
    s_wifi_state = NULL;
    s_wifi_ip = NULL;
    s_wifi_rssi = NULL;
    s_temp_label = NULL;
    s_touch_label = NULL;
    s_fps_label = NULL;
    s_touch_overlay = NULL;
    s_touch_dot = NULL;

    bsp_display_unlock();
}
