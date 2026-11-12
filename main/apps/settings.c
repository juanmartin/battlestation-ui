#include "apps.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "audio_service.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "settings.h"
#include "temperature_service.h"
#include "time_service.h"
#include "ui_theme.h"
#include "wifi_probe.h"

static const char *TAG = "settings";

#define COLOR_BG 0x0B0F14
#define COLOR_BUTTON 0x1B2430
#define COLOR_BUTTON_PRESSED 0x243040
#define COLOR_BORDER 0x2A3542
#define COLOR_TEXT 0xE2E8F0
#define COLOR_MUTED 0x94A3B8
#define COLOR_DIM 0x64748B
#define COLOR_CYAN 0x22D3EE
#define COLOR_GREEN 0x4ADE80
#define COLOR_AMBER 0xFBBF24
#define COLOR_RED 0xF87171

#define PAGE_Y 136
#define PAGE_W 1024
#define PAGE_H 440

#define TIMER_PERIOD_MS 500
#define ARMED_TIMEOUT_MS 8000u
#define SCAN_ROWS 8
#define TZ_COUNT 12

typedef struct {
    const char *label;
    const char *posix;
} tz_option_t;

typedef enum {
    TAB_DISPLAY = 0,
    TAB_NETWORK,
    TAB_HOME_ASSISTANT,
    TAB_SYSTEM,
    TAB_COUNT,
} settings_tab_t;

typedef enum {
    FIELD_WIFI_SSID = 0,
    FIELD_WIFI_PASSWORD,
    FIELD_HA_URL,
    FIELD_HA_TOKEN,
    FIELD_LIGHT_0,
    FIELD_LIGHT_1,
    FIELD_LIGHT_2,
    FIELD_LIGHT_3,
    FIELD_WEATHER,
    FIELD_COUNT,
} field_id_t;

typedef struct {
    lv_obj_t *btn;
    lv_obj_t *label;
} settings_button_t;

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *title;
    lv_obj_t *textarea;
    lv_obj_t *keyboard;
    int field;
    bool open;
} editor_ui_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *pages[TAB_COUNT];
    settings_button_t tabs[TAB_COUNT];
    settings_tab_t tab;

    /* Display / audio page. */
    lv_obj_t *brightness_value;
    lv_obj_t *brightness_slider;
    lv_obj_t *mic_value;
    lv_obj_t *mic_slider;
    lv_obj_t *volume_value;
    lv_obj_t *volume_slider;
    lv_obj_t *a4_value;
    lv_obj_t *a4_slider;
    lv_obj_t *da_status;

    /* Network page. */
    lv_obj_t *wifi_state;
    lv_obj_t *wifi_ip;
    lv_obj_t *wifi_rssi;
    lv_obj_t *scan_status;
    settings_button_t scan_button;
    lv_obj_t *scan_rows[SCAN_ROWS];
    lv_obj_t *scan_labels[SCAN_ROWS];
    wifi_probe_scan_ap_t scan_aps[SCAN_ROWS];
    size_t scan_count;
    bool scan_applied;
    lv_obj_t *net_status;

    /* Home Assistant page. */
    lv_obj_t *ha_status;
    settings_button_t ha_save;
    settings_button_t ha_restart;
    bool restart_armed;
    uint32_t restart_arm_until;

    /* Shared text fields (network and Home Assistant). */
    lv_obj_t *field_buttons[FIELD_COUNT];
    lv_obj_t *field_labels[FIELD_COUNT];

    /* System page. */
    settings_button_t tz_buttons[TZ_COUNT];
    lv_obj_t *tz_status;
    lv_obj_t *tz_active;
    lv_obj_t *clock_label;
    lv_obj_t *temp_value;
    settings_button_t reset_button;
    lv_obj_t *sys_status;
    bool reset_armed;
    uint32_t reset_arm_until;

    editor_ui_t editor;
    bool applying;
} settings_ui_t;

static settings_ui_t s_ui;
static app_settings_t s_edit;
static lv_timer_t *s_timer;

static const tz_option_t s_tz_options[TZ_COUNT] = {
    {"UTC-3 (Argentina)", "<-03>3"},
    {"UTC", "UTC0"},
    {"UTC-4", "<-04>4"},
    {"UTC-5", "<-05>5"},
    {"UK (GMT/BST)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe (CET)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe (EET)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"India (IST)", "IST-5:30"},
    {"China/Singapore", "CST-8"},
    {"Japan/Korea", "JST-9"},
    {"US Eastern (ET)", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Pacific (PT)", "PST8PDT,M3.2.0,M11.1.0"},
};

static const char *const s_field_names[FIELD_COUNT] = {
    "SSID",     "PASSWORD", "BASE URL", "TOKEN",    "LIGHT 0",
    "LIGHT 1",  "LIGHT 2",  "LIGHT 3",  "WEATHER",
};

static void refresh_display_values(void);
static void refresh_all_fields(void);
static void refresh_timezone_buttons(void);
static void refresh_wifi_status(void);
static void refresh_scan_ui(void);
static void update_temperature(void);
static void editor_open(int field);
static void editor_hide(void);
static void editor_commit(void);
static void disarm_restart(bool announce);
static void disarm_reset(bool announce);

static void label_set(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL) {
        return;
    }
    const char *current = lv_label_get_text(label);
    if (current == NULL || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void status_set(lv_obj_t *label, uint32_t color, const char *text)
{
    if (label == NULL || text == NULL) {
        return;
    }
    label_set(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

static void button_style(settings_button_t *button, bool selected,
                         uint32_t accent)
{
    if (button->btn == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(button->btn,
                              lv_color_hex(selected ? accent : COLOR_BUTTON), 0);
    lv_obj_set_style_bg_opa(button->btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button->btn,
                                  lv_color_hex(selected ? accent : COLOR_BORDER),
                                  0);
    if (button->label != NULL) {
        lv_obj_set_style_text_color(button->label,
                                    lv_color_hex(selected ? COLOR_BG : COLOR_TEXT),
                                    0);
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int w, int h, const lv_font_t *font,
                             lv_event_cb_t cb, void *user_data,
                             settings_button_t *out)
{
    lv_obj_t *btn = ui_theme_card(parent, x, y, w, h);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BUTTON), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BUTTON_PRESSED),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);

    lv_obj_t *label = ui_theme_label(btn, text, font, COLOR_TEXT, 0, 0);
    lv_obj_center(label);

    if (out != NULL) {
        out->btn = btn;
        out->label = label;
    }
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

static lv_obj_t *make_slider(lv_obj_t *parent, int x, int y, int w, int h,
                             int min, int max, int value, lv_event_cb_t cb)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, h);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_CYAN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLLABLE);
    if (cb != NULL) {
        /* VALUE_CHANGED is a live preview only. NVS is written on RELEASED. */
        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(slider, cb, LV_EVENT_RELEASED, NULL);
    }
    return slider;
}

static lv_obj_t *make_page(lv_obj_t *parent, settings_tab_t tab)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_pos(page, 0, PAGE_Y);
    lv_obj_set_size(page, PAGE_W, PAGE_H);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    s_ui.pages[tab] = page;
    return page;
}

static char *field_buffer(field_id_t field, size_t *size, bool *masked)
{
    char *buffer = NULL;
    size_t length = 0;
    bool secret = false;

    switch (field) {
    case FIELD_WIFI_SSID:
        buffer = s_edit.wifi_ssid;
        length = sizeof(s_edit.wifi_ssid);
        break;
    case FIELD_WIFI_PASSWORD:
        buffer = s_edit.wifi_password;
        length = sizeof(s_edit.wifi_password);
        secret = true;
        break;
    case FIELD_HA_URL:
        buffer = s_edit.ha_url;
        length = sizeof(s_edit.ha_url);
        break;
    case FIELD_HA_TOKEN:
        buffer = s_edit.ha_token;
        length = sizeof(s_edit.ha_token);
        secret = true;
        break;
    case FIELD_LIGHT_0:
        buffer = s_edit.light_entity[0];
        length = sizeof(s_edit.light_entity[0]);
        break;
    case FIELD_LIGHT_1:
        buffer = s_edit.light_entity[1];
        length = sizeof(s_edit.light_entity[1]);
        break;
    case FIELD_LIGHT_2:
        buffer = s_edit.light_entity[2];
        length = sizeof(s_edit.light_entity[2]);
        break;
    case FIELD_LIGHT_3:
        buffer = s_edit.light_entity[3];
        length = sizeof(s_edit.light_entity[3]);
        break;
    case FIELD_WEATHER:
        buffer = s_edit.weather_entity;
        length = sizeof(s_edit.weather_entity);
        break;
    default:
        break;
    }

    if (size != NULL) {
        *size = length;
    }
    if (masked != NULL) {
        *masked = secret;
    }
    return buffer;
}

static void refresh_field(int field)
{
    if (field < 0 || field >= FIELD_COUNT || s_ui.field_labels[field] == NULL) {
        return;
    }

    size_t size = 0;
    bool masked = false;
    const char *value = field_buffer((field_id_t)field, &size, &masked);
    char display[SETTINGS_HA_URL_MAX];

    if (value == NULL || size == 0u) {
        snprintf(display, sizeof(display), "(unavailable)");
    } else if (value[0] == '\0') {
        snprintf(display, sizeof(display), "(empty)");
    } else if (masked) {
        snprintf(display, sizeof(display), "********");
    } else {
        snprintf(display, sizeof(display), "%s", value);
    }
    label_set(s_ui.field_labels[field], display);
}

static void refresh_all_fields(void)
{
    for (int i = 0; i < FIELD_COUNT; i++) {
        refresh_field(i);
    }
}

static void refresh_display_values(void)
{
    char buf[24];

    s_ui.applying = true;

    lv_slider_set_value(s_ui.brightness_slider, s_edit.brightness, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", s_edit.brightness);
    label_set(s_ui.brightness_value, buf);

    lv_slider_set_value(s_ui.mic_slider, s_edit.mic_gain_db, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d dB", s_edit.mic_gain_db);
    label_set(s_ui.mic_value, buf);

    lv_slider_set_value(s_ui.volume_slider, s_edit.output_volume, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", s_edit.output_volume);
    label_set(s_ui.volume_value, buf);

    const int a4_half = (int)(s_edit.a4_hz * 2.0f + 0.5f);
    lv_slider_set_value(s_ui.a4_slider, a4_half, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%.1f Hz", (double)s_edit.a4_hz);
    label_set(s_ui.a4_value, buf);

    s_ui.applying = false;
}

static esp_err_t persist_current(void)
{
    esp_err_t err = settings_update(&s_edit);
    if (err != ESP_OK) {
        return err;
    }
    return settings_save();
}

static void persist_with_message(const char *ok_message)
{
    const esp_err_t err = persist_current();
    char buf[96];

    if (err == ESP_OK) {
        status_set(s_ui.da_status, COLOR_GREEN, ok_message);
    } else {
        snprintf(buf, sizeof(buf), "Save failed: %s", esp_err_to_name(err));
        status_set(s_ui.da_status, COLOR_RED, buf);
    }
}

static bool ha_url_valid(void)
{
    const char *url = s_edit.ha_url;
    if (url[0] == '\0') {
        return true;
    }
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

static bool ha_url_is_https(void)
{
    return strncmp(s_edit.ha_url, "https://", 8) == 0;
}

static void disarm_restart(bool announce)
{
    if (!s_ui.restart_armed) {
        return;
    }
    s_ui.restart_armed = false;
    label_set(s_ui.ha_restart.label, "SAVE + RESTART");
    button_style(&s_ui.ha_restart, false, COLOR_AMBER);
    if (announce) {
        status_set(s_ui.ha_status, COLOR_AMBER, "Restart cancelled.");
    }
}

static void disarm_reset(bool announce)
{
    if (!s_ui.reset_armed) {
        return;
    }
    s_ui.reset_armed = false;
    label_set(s_ui.reset_button.label, "RESET DEFAULTS");
    button_style(&s_ui.reset_button, false, COLOR_RED);
    if (announce) {
        status_set(s_ui.sys_status, COLOR_AMBER, "Defaults reset cancelled.");
    }
}

/* ------------------------------------------------------------------------- */
/* Shared text editor                                                        */
/* ------------------------------------------------------------------------- */

static void editor_hide(void)
{
    if (s_ui.editor.overlay == NULL) {
        return;
    }

    lv_obj_add_flag(s_ui.editor.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_ui.editor.keyboard, NULL);
    lv_textarea_set_text(s_ui.editor.textarea, "");
    lv_textarea_set_password_mode(s_ui.editor.textarea, false);
    lv_obj_remove_state(s_ui.editor.textarea, LV_STATE_FOCUSED);
    s_ui.editor.open = false;
    s_ui.editor.field = -1;
}

static void editor_commit(void)
{
    if (!s_ui.editor.open) {
        return;
    }

    const int field = s_ui.editor.field;
    const char *text = lv_textarea_get_text(s_ui.editor.textarea);

    if (field >= 0 && field < FIELD_COUNT) {
        size_t size = 0;
        bool masked = false;
        char *dst = field_buffer((field_id_t)field, &size, &masked);
        if (dst != NULL && size > 0u) {
            snprintf(dst, size, "%s", text != NULL ? text : "");
        }
        refresh_field(field);
    }

    disarm_restart(false);
    disarm_reset(false);
    editor_hide();
}

static void editor_open(int field)
{
    if (s_ui.editor.overlay == NULL || field < 0 || field >= FIELD_COUNT) {
        return;
    }

    size_t size = 0;
    bool masked = false;
    const char *value = field_buffer((field_id_t)field, &size, &masked);
    if (value == NULL || size == 0u) {
        return;
    }

    char title[40];
    snprintf(title, sizeof(title), "EDIT %s", s_field_names[field]);
    label_set(s_ui.editor.title, title);

    s_ui.editor.field = field;
    s_ui.editor.open = true;

    lv_textarea_set_password_mode(s_ui.editor.textarea, masked);
    lv_textarea_set_max_length(s_ui.editor.textarea, (uint32_t)(size - 1u));
    lv_textarea_set_text(s_ui.editor.textarea, value);
    lv_obj_add_state(s_ui.editor.textarea, LV_STATE_FOCUSED);

    lv_keyboard_set_mode(s_ui.editor.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_ui.editor.keyboard, s_ui.editor.textarea);

    lv_obj_remove_flag(s_ui.editor.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.editor.overlay);
}

static void editor_keyboard_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        editor_commit();
    } else if (code == LV_EVENT_CANCEL) {
        editor_hide();
    }
}

static void editor_save_cb(lv_event_t *e)
{
    (void)e;
    editor_commit();
}

static void editor_cancel_cb(lv_event_t *e)
{
    (void)e;
    editor_hide();
}

static void field_clicked_cb(lv_event_t *e)
{
    editor_open((int)(intptr_t)lv_event_get_user_data(e));
}

static void build_editor(lv_obj_t *parent)
{
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);

    /* Keep the top-right corner free for the global HOME button. */
    lv_obj_t *panel = ui_theme_card(overlay, 32, 64, 960, 170);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x151B23), 0);

    s_ui.editor.title = ui_theme_label(panel, "EDIT", &lv_font_montserrat_20,
                                       COLOR_CYAN, 20, 12);

    lv_obj_t *ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_pos(ta, 20, 46);
    lv_obj_set_size(ta, 920, 56);
    lv_textarea_set_max_length(ta, SETTINGS_HA_URL_MAX - 1u);
    lv_obj_set_style_bg_color(ta, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_pad_all(ta, 8, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(COLOR_CYAN), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_add_event_cb(ta, editor_keyboard_cb, LV_EVENT_READY, NULL);
    s_ui.editor.textarea = ta;

    make_button(panel, "SAVE", 20, 112, 200, 44, &lv_font_montserrat_20,
                editor_save_cb, NULL, NULL);
    make_button(panel, "CANCEL", 740, 112, 200, 44, &lv_font_montserrat_20,
                editor_cancel_cb, NULL, NULL);
    lv_obj_t *hint = ui_theme_label(panel, "Enter text, then SAVE.",
                                    &lv_font_montserrat_14, COLOR_DIM,
                                    240, 126);
    lv_obj_set_width(hint, 460);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    lv_obj_t *kb = lv_keyboard_create(overlay);
    lv_obj_set_align(kb, LV_ALIGN_DEFAULT);
    lv_obj_set_pos(kb, 0, 234);
    lv_obj_set_size(kb, BSP_LCD_H_RES, 360);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_BUTTON), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb, 1, 0);
    lv_obj_set_style_border_color(kb, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(kb, 8, 0);
    lv_obj_set_style_pad_all(kb, 6, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_BUTTON), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(COLOR_BUTTON_PRESSED),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(kb, editor_keyboard_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, editor_keyboard_cb, LV_EVENT_CANCEL, NULL);
    s_ui.editor.keyboard = kb;

    s_ui.editor.field = -1;
    s_ui.editor.open = false;
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    s_ui.editor.overlay = overlay;
}

static void build_field_row(lv_obj_t *parent, int field,
                            int label_x, int label_y,
                            int x, int y, int w, int h)
{
    ui_theme_label(parent, s_field_names[field], &lv_font_montserrat_14,
                   COLOR_MUTED, label_x, label_y);

    settings_button_t row;
    make_button(parent, "", x, y, w, h, &lv_font_montserrat_14,
                field_clicked_cb, (void *)(intptr_t)field, &row);
    lv_obj_set_width(row.label, w - 24);
    lv_obj_align(row.label, LV_ALIGN_LEFT_MID, 12, 0);
    lv_label_set_long_mode(row.label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(row.label, LV_TEXT_ALIGN_LEFT, 0);

    s_ui.field_buttons[field] = row.btn;
    s_ui.field_labels[field] = row.label;
    refresh_field(field);
}

/* ------------------------------------------------------------------------- */
/* Display / audio                                                           */
/* ------------------------------------------------------------------------- */

static void brightness_cb(lv_event_t *e)
{
    if (s_ui.applying) {
        return;
    }

    const int value = (int)lv_slider_get_value(lv_event_get_target(e));
    s_edit.brightness = value;
    (void)bsp_display_brightness_set(value);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", value);
    label_set(s_ui.brightness_value, buf);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        persist_with_message("Brightness saved.");
    }
}

static void mic_gain_cb(lv_event_t *e)
{
    if (s_ui.applying) {
        return;
    }

    const int value = (int)lv_slider_get_value(lv_event_get_target(e));
    s_edit.mic_gain_db = value;
    const esp_err_t err = audio_service_set_mic_gain((float)value);

    char buf[96];
    snprintf(buf, sizeof(buf), "%d dB", value);
    label_set(s_ui.mic_value, buf);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        if (err == ESP_OK) {
            persist_with_message("Microphone gain saved.");
        } else {
            const esp_err_t save_err = persist_current();
            if (save_err == ESP_OK) {
                snprintf(buf, sizeof(buf),
                         "Mic gain saved; live apply failed: %s",
                         esp_err_to_name(err));
                status_set(s_ui.da_status, COLOR_AMBER, buf);
            } else {
                snprintf(buf, sizeof(buf), "Save failed: %s",
                         esp_err_to_name(save_err));
                status_set(s_ui.da_status, COLOR_RED, buf);
            }
        }
    }
}

static void output_volume_cb(lv_event_t *e)
{
    if (s_ui.applying) {
        return;
    }

    const int value = (int)lv_slider_get_value(lv_event_get_target(e));
    s_edit.output_volume = value;
    const esp_err_t err = audio_service_set_output_volume(value);

    char buf[96];
    snprintf(buf, sizeof(buf), "%d%%", value);
    label_set(s_ui.volume_value, buf);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        if (err == ESP_OK) {
            persist_with_message("Output volume saved.");
        } else {
            const esp_err_t save_err = persist_current();
            if (save_err == ESP_OK) {
                snprintf(buf, sizeof(buf),
                         "Volume saved; live apply failed: %s",
                         esp_err_to_name(err));
                status_set(s_ui.da_status, COLOR_AMBER, buf);
            } else {
                snprintf(buf, sizeof(buf), "Save failed: %s",
                         esp_err_to_name(save_err));
                status_set(s_ui.da_status, COLOR_RED, buf);
            }
        }
    }
}

static void a4_cb(lv_event_t *e)
{
    if (s_ui.applying) {
        return;
    }

    const int value = (int)lv_slider_get_value(lv_event_get_target(e));
    s_edit.a4_hz = (float)value / 2.0f;

    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f Hz", (double)s_edit.a4_hz);
    label_set(s_ui.a4_value, buf);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        persist_with_message("A4 reference saved; used next time Tuner opens.");
    }
}

static void save_display_cb(lv_event_t *e)
{
    (void)e;
    const esp_err_t err = persist_current();

    if (err == ESP_OK) {
        status_set(s_ui.da_status, COLOR_GREEN,
                   "Display and audio settings saved to NVS.");
    } else {
        char buf[96];
        snprintf(buf, sizeof(buf), "Save failed: %s", esp_err_to_name(err));
        status_set(s_ui.da_status, COLOR_RED, buf);
    }
}

static void build_display_page(lv_obj_t *page)
{
    lv_obj_t *card = ui_theme_card(page, 32, 0, 480, PAGE_H);
    ui_theme_label(card, "DISPLAY / AUDIO", &lv_font_montserrat_20, COLOR_CYAN,
                   24, 16);

    ui_theme_label(card, "BRIGHTNESS", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 56);
    s_ui.brightness_value = ui_theme_label(card, "", &lv_font_montserrat_20,
                                           COLOR_TEXT, 356, 52);
    lv_obj_set_width(s_ui.brightness_value, 100);
    lv_obj_set_style_text_align(s_ui.brightness_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.brightness_slider = make_slider(card, 24, 86, 432, 16, 10, 100,
                                         s_edit.brightness, brightness_cb);

    ui_theme_label(card, "MICROPHONE GAIN", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 122);
    s_ui.mic_value = ui_theme_label(card, "", &lv_font_montserrat_20,
                                    COLOR_TEXT, 356, 118);
    lv_obj_set_width(s_ui.mic_value, 100);
    lv_obj_set_style_text_align(s_ui.mic_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.mic_slider = make_slider(card, 24, 152, 432, 16, 0, 37,
                                  s_edit.mic_gain_db, mic_gain_cb);

    ui_theme_label(card, "OUTPUT VOLUME", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 188);
    s_ui.volume_value = ui_theme_label(card, "", &lv_font_montserrat_20,
                                       COLOR_TEXT, 356, 184);
    lv_obj_set_width(s_ui.volume_value, 100);
    lv_obj_set_style_text_align(s_ui.volume_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.volume_slider = make_slider(card, 24, 218, 432, 16, 0, 100,
                                     s_edit.output_volume, output_volume_cb);

    const int a4_half = (int)(s_edit.a4_hz * 2.0f + 0.5f);
    ui_theme_label(card, "A4 REFERENCE", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 254);
    s_ui.a4_value = ui_theme_label(card, "", &lv_font_montserrat_20,
                                   COLOR_TEXT, 346, 250);
    lv_obj_set_width(s_ui.a4_value, 110);
    lv_obj_set_style_text_align(s_ui.a4_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.a4_slider = make_slider(card, 24, 284, 432, 16, 860, 900, a4_half,
                                 a4_cb);

    ui_theme_label(card, "A4 is used next time the Tuner app opens.",
                   &lv_font_montserrat_14, COLOR_DIM, 24, 310);

    make_button(card, "SAVE DISPLAY / AUDIO", 24, 336, 300, 52,
                &lv_font_montserrat_20, save_display_cb, NULL, NULL);
    s_ui.da_status = ui_theme_label(card, "", &lv_font_montserrat_14,
                                    COLOR_MUTED, 24, 400);
    lv_obj_set_width(s_ui.da_status, 432);
    lv_label_set_long_mode(s_ui.da_status, LV_LABEL_LONG_WRAP);

    lv_obj_t *notes = ui_theme_card(page, 532, 0, 460, PAGE_H);
    ui_theme_label(notes, "LIVE APPLY", &lv_font_montserrat_20, COLOR_GREEN,
                   20, 16);
    lv_obj_t *body = ui_theme_label(
        notes,
        "Brightness, microphone gain and output volume apply while you drag. "
        "The local settings copy changes during the drag; the NVS blob is "
        "written once when the slider is released or SAVE is pressed.\n\n"
        "Every save calls settings_update() first, so values are validated "
        "and clamped before settings_save() writes them.\n\n"
        "The A4 reference is stored only. It takes effect the next time the "
        "Tuner app starts.\n\n"
        "RESET DEFAULTS on the SYSTEM tab restores the Kconfig values for "
        "every setting in this app.",
        &lv_font_montserrat_14, COLOR_MUTED, 20, 52);
    lv_obj_set_width(body, 420);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
}

/* ------------------------------------------------------------------------- */
/* Network                                                                   */
/* ------------------------------------------------------------------------- */

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

static void refresh_wifi_status(void)
{
    wifi_probe_snapshot_t snap;
    wifi_probe_get(&snap);

    label_set(s_ui.wifi_state, wifi_state_text(snap.state));
    lv_obj_set_style_text_color(s_ui.wifi_state,
                                lv_color_hex(wifi_state_color(snap.state)), 0);

    char buf[64];
    snprintf(buf, sizeof(buf), "IP: %s", snap.ip[0] != '\0' ? snap.ip : "---");
    label_set(s_ui.wifi_ip, buf);

    snprintf(buf, sizeof(buf), "RSSI: %d dBm",
             snap.state == WIFI_PROBE_CONNECTED ? snap.rssi : 0);
    label_set(s_ui.wifi_rssi, buf);
}

static void scan_result_cb(lv_event_t *e)
{
    const size_t index = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (index >= s_ui.scan_count) {
        return;
    }

    snprintf(s_edit.wifi_ssid, sizeof(s_edit.wifi_ssid), "%s",
             s_ui.scan_aps[index].ssid);
    refresh_field(FIELD_WIFI_SSID);
    status_set(s_ui.net_status, COLOR_CYAN,
               "SSID selected. Enter the password, then press CONNECT.");
}

static void scan_clicked_cb(lv_event_t *e)
{
    (void)e;

    const esp_err_t err = wifi_probe_scan_start();
    if (err != ESP_OK) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Scan unavailable: %s",
                 esp_err_to_name(err));
        status_set(s_ui.scan_status, COLOR_RED, buf);
        return;
    }

    s_ui.scan_applied = false;
    s_ui.scan_count = 0;
    memset(s_ui.scan_aps, 0, sizeof(s_ui.scan_aps));
    for (int i = 0; i < SCAN_ROWS; i++) {
        lv_obj_add_flag(s_ui.scan_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    status_set(s_ui.scan_status, COLOR_AMBER, "scanning...");
}

static void refresh_scan_ui(void)
{
    wifi_probe_scan_t scan;
    wifi_probe_scan_get(&scan);

    if (scan.running) {
        s_ui.scan_applied = false;
        status_set(s_ui.scan_status, COLOR_AMBER, "scanning...");
        return;
    }

    if (!scan.done || s_ui.scan_applied) {
        return;
    }

    s_ui.scan_applied = true;
    s_ui.scan_count = 0;
    memset(s_ui.scan_aps, 0, sizeof(s_ui.scan_aps));

    for (size_t i = 0; i < scan.count && s_ui.scan_count < SCAN_ROWS; i++) {
        if (scan.aps[i].ssid[0] == '\0') {
            continue;
        }
        s_ui.scan_aps[s_ui.scan_count] = scan.aps[i];
        s_ui.scan_count++;
    }

    for (int i = 0; i < SCAN_ROWS; i++) {
        if ((size_t)i < s_ui.scan_count) {
            char text[80];
            const wifi_probe_scan_ap_t *ap = &s_ui.scan_aps[i];
            snprintf(text, sizeof(text), "%s   %d dBm%s", ap->ssid,
                     (int)ap->rssi, ap->secure ? "   secure" : "");
            label_set(s_ui.scan_labels[i], text);
            lv_obj_remove_flag(s_ui.scan_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.scan_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    char status[48];
    if (s_ui.scan_count == 0u) {
        snprintf(status, sizeof(status), "no 2.4 GHz networks found");
    } else {
        snprintf(status, sizeof(status), "%u networks found",
                 (unsigned)s_ui.scan_count);
    }
    status_set(s_ui.scan_status, COLOR_MUTED, status);
}

static void connect_clicked_cb(lv_event_t *e)
{
    (void)e;

    const bool empty_ssid = s_edit.wifi_ssid[0] == '\0';
    const esp_err_t wifi_err = wifi_probe_set_credentials(
        s_edit.wifi_ssid, s_edit.wifi_password);
    const esp_err_t save_err = persist_current();
    char buf[128];

    if (wifi_err != ESP_OK) {
        snprintf(buf, sizeof(buf),
                 "Wi-Fi apply failed: %s. Credentials were stored.",
                 esp_err_to_name(wifi_err));
        status_set(s_ui.net_status, COLOR_RED, buf);
        return;
    }
    if (save_err != ESP_OK) {
        snprintf(buf, sizeof(buf), "Wi-Fi applied but save failed: %s",
                 esp_err_to_name(save_err));
        status_set(s_ui.net_status, COLOR_AMBER, buf);
        return;
    }
    if (empty_ssid) {
        status_set(s_ui.net_status, COLOR_AMBER,
                   "SSID is empty: Wi-Fi is disabled. Settings saved.");
        return;
    }

    snprintf(buf, sizeof(buf),
             "Connecting to '%s'. Credentials saved to NVS.",
             s_edit.wifi_ssid);
    status_set(s_ui.net_status, COLOR_GREEN, buf);
}

static void build_network_page(lv_obj_t *page)
{
    lv_obj_t *status = ui_theme_card(page, 32, 0, 480, PAGE_H);
    ui_theme_label(status, "WI-FI STATUS", &lv_font_montserrat_20, COLOR_CYAN,
                   20, 16);

    s_ui.wifi_state = ui_theme_label(status, "initializing...",
                                     &lv_font_montserrat_20, COLOR_AMBER,
                                     20, 48);
    s_ui.wifi_ip = ui_theme_label(status, "IP: ---", &lv_font_montserrat_14,
                                  COLOR_TEXT, 20, 80);
    s_ui.wifi_rssi = ui_theme_label(status, "RSSI: 0 dBm",
                                    &lv_font_montserrat_14, COLOR_MUTED,
                                    20, 102);

    make_button(status, "SCAN", 340, 44, 116, 40, &lv_font_montserrat_14,
                scan_clicked_cb, NULL, &s_ui.scan_button);

    ui_theme_label(status, "NEARBY NETWORKS", &lv_font_montserrat_14,
                   COLOR_MUTED, 20, 132);
    s_ui.scan_status = ui_theme_label(status, "tap SCAN",
                                      &lv_font_montserrat_14, COLOR_DIM,
                                      190, 132);
    lv_obj_set_width(s_ui.scan_status, 270);
    lv_obj_set_style_text_align(s_ui.scan_status, LV_TEXT_ALIGN_RIGHT, 0);

    for (int i = 0; i < SCAN_ROWS; i++) {
        settings_button_t row;
        make_button(status, "", 20, 160 + i * 30, 440, 28,
                    &lv_font_montserrat_14, scan_result_cb,
                    (void *)(intptr_t)i, &row);
        lv_obj_set_width(row.label, 416);
        lv_obj_align(row.label, LV_ALIGN_LEFT_MID, 10, 0);
        lv_label_set_long_mode(row.label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(row.label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_radius(row.btn, 8, 0);
        lv_obj_add_flag(row.btn, LV_OBJ_FLAG_HIDDEN);
        s_ui.scan_rows[i] = row.btn;
        s_ui.scan_labels[i] = row.label;
    }

    ui_theme_label(status, "2.4 GHz only. Tap a network to fill the SSID field.",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 410);

    lv_obj_t *conn = ui_theme_card(page, 532, 0, 460, PAGE_H);
    ui_theme_label(conn, "CONNECTION", &lv_font_montserrat_20, COLOR_CYAN,
                   20, 16);

    build_field_row(conn, FIELD_WIFI_SSID, 20, 52, 20, 70, 420, 44);
    build_field_row(conn, FIELD_WIFI_PASSWORD, 20, 124, 20, 142, 420, 44);

    make_button(conn, "CONNECT", 20, 200, 420, 56, &lv_font_montserrat_20,
                connect_clicked_cb, NULL, NULL);

    s_ui.net_status = ui_theme_label(conn, "", &lv_font_montserrat_14,
                                     COLOR_MUTED, 20, 266);
    lv_obj_set_width(s_ui.net_status, 420);
    lv_label_set_long_mode(s_ui.net_status, LV_LABEL_LONG_WRAP);

    lv_obj_t *hint = ui_theme_label(
        conn,
        "Credentials are saved to NVS and applied immediately. An empty SSID "
        "disables Wi-Fi. The ESP32-C6 supports 2.4 GHz networks only. The "
        "password is never written to logs.",
        &lv_font_montserrat_14, COLOR_DIM, 20, 332);
    lv_obj_set_width(hint, 420);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
}

/* ------------------------------------------------------------------------- */
/* Home Assistant                                                            */
/* ------------------------------------------------------------------------- */

static void save_ha_cb(lv_event_t *e)
{
    (void)e;

    if (!ha_url_valid()) {
        disarm_restart(false);
        status_set(s_ui.ha_status, COLOR_RED,
                   "URL must start with http:// or https://. Not saved.");
        return;
    }

    const esp_err_t err = persist_current();
    disarm_restart(false);

    if (err != ESP_OK) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Save failed: %s", esp_err_to_name(err));
        status_set(s_ui.ha_status, COLOR_RED, buf);
        return;
    }

    if (ha_url_is_https()) {
        status_set(s_ui.ha_status, COLOR_AMBER,
                   "Saved with warning: https:// is not supported by the plain "
                   "HTTP transport. Restart to apply Home Assistant changes.");
    } else {
        status_set(s_ui.ha_status, COLOR_GREEN,
                   "Saved. Restart to apply Home Assistant changes.");
    }
}

static void save_restart_cb(lv_event_t *e)
{
    (void)e;

    if (!s_ui.restart_armed) {
        if (!ha_url_valid()) {
            status_set(s_ui.ha_status, COLOR_RED,
                       "URL must start with http:// or https://. Cannot restart "
                       "with this URL.");
            return;
        }
        s_ui.restart_armed = true;
        s_ui.restart_arm_until = lv_tick_get() + ARMED_TIMEOUT_MS;
        label_set(s_ui.ha_restart.label, "CONFIRM RESTART");
        button_style(&s_ui.ha_restart, true, COLOR_AMBER);
        disarm_reset(false);
        status_set(s_ui.ha_status, COLOR_AMBER,
                   "Tap SAVE + RESTART again to save and reboot now.");
        return;
    }

    const esp_err_t err = persist_current();
    if (err != ESP_OK) {
        disarm_restart(false);
        char buf[96];
        snprintf(buf, sizeof(buf), "Save failed: %s. Not restarting.",
                 esp_err_to_name(err));
        status_set(s_ui.ha_status, COLOR_RED, buf);
        return;
    }

    ESP_LOGI(TAG, "restart requested from settings");
    esp_restart();
}

static void build_ha_page(lv_obj_t *page)
{
    lv_obj_t *fields = ui_theme_card(page, 32, 0, 560, PAGE_H);
    ui_theme_label(fields, "HOME ASSISTANT", &lv_font_montserrat_20,
                   COLOR_CYAN, 20, 16);

    build_field_row(fields, FIELD_HA_URL, 20, 56, 130, 46, 410, 36);
    build_field_row(fields, FIELD_HA_TOKEN, 20, 102, 130, 92, 410, 36);
    build_field_row(fields, FIELD_LIGHT_0, 20, 148, 130, 138, 410, 36);
    build_field_row(fields, FIELD_LIGHT_1, 20, 194, 130, 184, 410, 36);
    build_field_row(fields, FIELD_LIGHT_2, 20, 240, 130, 230, 410, 36);
    build_field_row(fields, FIELD_LIGHT_3, 20, 286, 130, 276, 410, 36);
    build_field_row(fields, FIELD_WEATHER, 20, 332, 130, 322, 410, 36);

    ui_theme_label(fields, "Tap a field to edit it with the on-screen keyboard.",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 374);
    ui_theme_label(fields, "The long-lived token is stored in NVS and never logged.",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 396);
    ui_theme_label(fields,
                   "Entity IDs look like light.kitchen or weather.home.",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 418);

    lv_obj_t *actions = ui_theme_card(page, 612, 0, 380, PAGE_H);
    ui_theme_label(actions, "APPLY", &lv_font_montserrat_20, COLOR_CYAN,
                   24, 16);

    make_button(actions, "SAVE HA", 24, 56, 332, 56, &lv_font_montserrat_20,
                save_ha_cb, NULL, &s_ui.ha_save);
    make_button(actions, "SAVE + RESTART", 24, 126, 332, 56,
                &lv_font_montserrat_20, save_restart_cb, NULL,
                &s_ui.ha_restart);

    s_ui.ha_status = ui_theme_label(actions, "", &lv_font_montserrat_14,
                                    COLOR_MUTED, 24, 198);
    lv_obj_set_width(s_ui.ha_status, 332);
    lv_label_set_long_mode(s_ui.ha_status, LV_LABEL_LONG_WRAP);

    lv_obj_t *info = ui_theme_label(
        actions,
        "The HA client is built at boot with the saved URL, token and entity "
        "IDs. SAVE only writes NVS; restart to apply changes.",
        &lv_font_montserrat_14, COLOR_DIM, 24, 300);
    lv_obj_set_width(info, 332);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

    lv_obj_t *warn = ui_theme_label(
        actions,
        "https:// is accepted but not supported: the M10 HA transport is "
        "plain HTTP only.",
        &lv_font_montserrat_14, COLOR_AMBER, 24, 372);
    lv_obj_set_width(warn, 332);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
}

/* ------------------------------------------------------------------------- */
/* System                                                                    */
/* ------------------------------------------------------------------------- */

static void timezone_clicked_cb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= TZ_COUNT) {
        return;
    }

    snprintf(s_edit.timezone, sizeof(s_edit.timezone), "%s",
             s_tz_options[index].posix);
    const esp_err_t tz_err = time_service_set_timezone(s_edit.timezone);
    const esp_err_t save_err = persist_current();

    refresh_timezone_buttons();
    disarm_reset(false);

    if (tz_err != ESP_OK) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Timezone apply failed: %s",
                 esp_err_to_name(tz_err));
        status_set(s_ui.tz_status, COLOR_RED, buf);
    } else if (save_err != ESP_OK) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Timezone applied but save failed: %s",
                 esp_err_to_name(save_err));
        status_set(s_ui.tz_status, COLOR_AMBER, buf);
    } else {
        status_set(s_ui.tz_status, COLOR_GREEN,
                   "Timezone applied and saved.");
    }
}

static void refresh_timezone_buttons(void)
{
    int active = -1;
    for (int i = 0; i < TZ_COUNT; i++) {
        if (strcmp(s_edit.timezone, s_tz_options[i].posix) == 0) {
            active = i;
        }
        button_style(&s_ui.tz_buttons[i],
                     strcmp(s_edit.timezone, s_tz_options[i].posix) == 0,
                     COLOR_CYAN);
    }

    char buf[64];
    if (active >= 0) {
        snprintf(buf, sizeof(buf), "Active: %s", s_tz_options[active].label);
    } else {
        snprintf(buf, sizeof(buf), "Active: %s", s_edit.timezone);
    }
    label_set(s_ui.tz_active, buf);
}

static void update_temperature(void)
{
    if (s_ui.temp_value == NULL) {
        return;
    }

    float celsius = 0.0f;
    char buf[24];
    if (temperature_service_read(&celsius)) {
        snprintf(buf, sizeof(buf), "%.1f C", (double)celsius);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    label_set(s_ui.temp_value, buf);
}

static void update_local_clock(void)
{
    if (s_ui.clock_label == NULL) {
        return;
    }

    struct tm now;
    char buf[40];
    if (time_service_get_local(&now)) {
        strftime(buf, sizeof(buf), "Local time: %H:%M:%S", &now);
    } else {
        snprintf(buf, sizeof(buf), "Local time: not synced");
    }
    label_set(s_ui.clock_label, buf);
}

static void reset_defaults_cb(lv_event_t *e)
{
    (void)e;

    if (!s_ui.reset_armed) {
        s_ui.reset_armed = true;
        s_ui.reset_arm_until = lv_tick_get() + ARMED_TIMEOUT_MS;
        label_set(s_ui.reset_button.label, "CONFIRM RESET");
        button_style(&s_ui.reset_button, true, COLOR_RED);
        disarm_restart(false);
        status_set(s_ui.sys_status, COLOR_AMBER,
                   "Tap RESET DEFAULTS again to restore Kconfig defaults.");
        return;
    }

    const esp_err_t err = settings_reset_defaults();
    if (err != ESP_OK) {
        disarm_reset(false);
        char buf[96];
        snprintf(buf, sizeof(buf), "Reset failed: %s", esp_err_to_name(err));
        status_set(s_ui.sys_status, COLOR_RED, buf);
        return;
    }

    const app_settings_t *current = settings_get();
    if (current == NULL) {
        disarm_reset(false);
        status_set(s_ui.sys_status, COLOR_RED,
                   "Reset failed: settings unavailable.");
        return;
    }
    s_edit = *current;

    esp_err_t live_err = bsp_display_brightness_set(s_edit.brightness);
    esp_err_t audio_err =
        audio_service_set_mic_gain((float)s_edit.mic_gain_db);
    if (live_err == ESP_OK) {
        live_err = audio_err;
    }
    audio_err = audio_service_set_output_volume(s_edit.output_volume);
    if (live_err == ESP_OK) {
        live_err = audio_err;
    }
    const esp_err_t tz_err = time_service_set_timezone(s_edit.timezone);
    if (live_err == ESP_OK) {
        live_err = tz_err;
    }

    refresh_display_values();
    refresh_all_fields();
    refresh_timezone_buttons();
    disarm_reset(false);

    if (live_err == ESP_OK) {
        status_set(s_ui.sys_status, COLOR_GREEN, "Defaults restored.");
    } else {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "Defaults restored; live apply failed: %s",
                 esp_err_to_name(live_err));
        status_set(s_ui.sys_status, COLOR_AMBER, buf);
    }
}

static void build_system_page(lv_obj_t *page)
{
    lv_obj_t *tz = ui_theme_card(page, 32, 0, 480, PAGE_H);
    ui_theme_label(tz, "TIMEZONE", &lv_font_montserrat_20, COLOR_CYAN,
                   20, 16);

    for (int i = 0; i < TZ_COUNT; i++) {
        const int col = i % 2;
        const int row = i / 2;
        make_button(tz, s_tz_options[i].label, 20 + col * 230,
                    40 + row * 38, 220, 36, &lv_font_montserrat_14,
                    timezone_clicked_cb, (void *)(intptr_t)i,
                    &s_ui.tz_buttons[i]);
    }

    s_ui.clock_label = ui_theme_label(tz, "Local time: not synced",
                                      &lv_font_montserrat_28, COLOR_TEXT,
                                      20, 272);
    lv_obj_set_width(s_ui.clock_label, 440);

    s_ui.tz_status = ui_theme_label(tz, "", &lv_font_montserrat_14,
                                    COLOR_MUTED, 20, 318);
    lv_obj_set_width(s_ui.tz_status, 440);
    lv_label_set_long_mode(s_ui.tz_status, LV_LABEL_LONG_WRAP);

    s_ui.tz_active = ui_theme_label(tz, "Active: --",
                                    &lv_font_montserrat_14, COLOR_TEXT,
                                    20, 344);
    lv_obj_set_width(s_ui.tz_active, 440);
    lv_label_set_long_mode(s_ui.tz_active, LV_LABEL_LONG_DOT);

    lv_obj_t *note = ui_theme_label(
        tz,
        "Applies immediately; saved to NVS.",
        &lv_font_montserrat_14, COLOR_DIM, 20, 374);
    lv_obj_set_width(note, 440);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);

    lv_obj_t *system = ui_theme_card(page, 532, 0, 460, PAGE_H);
    ui_theme_label(system, "SYSTEM", &lv_font_montserrat_20, COLOR_CYAN,
                   24, 16);

    ui_theme_label(system, "P4 DIE TEMPERATURE", &lv_font_montserrat_14,
                   COLOR_MUTED, 24, 52);
    s_ui.temp_value = ui_theme_label(system, "--", &lv_font_montserrat_48,
                                     COLOR_TEXT, 24, 74);
    ui_theme_label(system, "Internal sensor; not ambient temperature.",
                   &lv_font_montserrat_14, COLOR_DIM, 24, 136);

    lv_obj_t *device = ui_theme_label(
        system,
        "Board: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B\n"
        "SoC: ESP32-P4 rev1.3  /  32 MB flash  /  32 MB PSRAM\n"
        "Firmware: M10  /  1024 x 600  /  ESP-IDF",
        &lv_font_montserrat_14, COLOR_MUTED, 24, 168);
    lv_obj_set_width(device, 412);
    lv_label_set_long_mode(device, LV_LABEL_LONG_WRAP);

    make_button(system, "RESET DEFAULTS", 24, 252, 300, 52,
                &lv_font_montserrat_20, reset_defaults_cb, NULL,
                &s_ui.reset_button);

    s_ui.sys_status = ui_theme_label(system, "", &lv_font_montserrat_14,
                                     COLOR_MUTED, 24, 318);
    lv_obj_set_width(s_ui.sys_status, 412);
    lv_label_set_long_mode(s_ui.sys_status, LV_LABEL_LONG_WRAP);

    lv_obj_t *reset_note = ui_theme_label(
        system,
        "Reset restores Kconfig defaults for every setting, applies brightness "
        "/ mic / volume / timezone live, and does not reboot.",
        &lv_font_montserrat_14, COLOR_DIM, 24, 386);
    lv_obj_set_width(reset_note, 412);
    lv_label_set_long_mode(reset_note, LV_LABEL_LONG_WRAP);
}

/* ------------------------------------------------------------------------- */
/* Tabs, timer and app lifecycle                                             */
/* ------------------------------------------------------------------------- */

static void tab_clicked_cb(lv_event_t *e)
{
    const settings_tab_t tab =
        (settings_tab_t)(intptr_t)lv_event_get_user_data(e);
    if (tab < TAB_DISPLAY || tab >= TAB_COUNT) {
        return;
    }

    for (int i = 0; i < TAB_COUNT; i++) {
        if (s_ui.pages[i] != NULL) {
            if ((settings_tab_t)i == tab) {
                lv_obj_remove_flag(s_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        button_style(&s_ui.tabs[i], (settings_tab_t)i == tab, COLOR_CYAN);
    }

    s_ui.tab = tab;
    disarm_restart(false);
    disarm_reset(false);

    if (tab == TAB_NETWORK) {
        refresh_wifi_status();
        refresh_scan_ui();
    } else if (tab == TAB_SYSTEM) {
        update_temperature();
        update_local_clock();
    }
}

static void settings_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    refresh_wifi_status();
    refresh_scan_ui();
    update_temperature();
    update_local_clock();

    const uint32_t now = lv_tick_get();
    if (s_ui.restart_armed &&
        (int32_t)(now - s_ui.restart_arm_until) >= 0) {
        disarm_restart(true);
    }
    if (s_ui.reset_armed &&
        (int32_t)(now - s_ui.reset_arm_until) >= 0) {
        disarm_reset(true);
    }
}

static void show_mount_error(lv_obj_t *parent)
{
    lv_obj_t *label = ui_theme_label(
        parent,
        "Settings unavailable: the settings store is not initialized.",
        &lv_font_montserrat_20, COLOR_RED, 32, 32);
    lv_obj_set_width(label, 900);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

esp_err_t settings_mount(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.tab = TAB_DISPLAY;
    s_ui.editor.field = -1;

    const app_settings_t *current = settings_get();
    if (current == NULL) {
        ESP_LOGE(TAG, "settings store is not initialized");
        show_mount_error(parent);
        return ESP_ERR_INVALID_STATE;
    }
    s_edit = *current;

    s_ui.root = lv_obj_create(parent);
    lv_obj_set_pos(s_ui.root, 0, 0);
    lv_obj_set_size(s_ui.root, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);

    ui_theme_label(s_ui.root, "SETTINGS", &lv_font_montserrat_28,
                   COLOR_TEXT, 32, 20);
    ui_theme_label(s_ui.root,
                   "Runtime configuration stored in NVS  /  M10",
                   &lv_font_montserrat_14, COLOR_MUTED, 34, 58);

    static const char *const tab_names[TAB_COUNT] = {
        "DISPLAY / AUDIO", "NETWORK", "HOME ASSISTANT", "SYSTEM"};
    static const int tab_x[TAB_COUNT] = {32, 232, 382, 582};
    static const int tab_w[TAB_COUNT] = {190, 140, 190, 140};

    for (int i = 0; i < TAB_COUNT; i++) {
        make_button(s_ui.root, tab_names[i], tab_x[i], 84, tab_w[i], 40,
                    &lv_font_montserrat_14, tab_clicked_cb,
                    (void *)(intptr_t)i, &s_ui.tabs[i]);
    }

    lv_obj_t *display_page = make_page(s_ui.root, TAB_DISPLAY);
    lv_obj_t *network_page = make_page(s_ui.root, TAB_NETWORK);
    lv_obj_t *ha_page = make_page(s_ui.root, TAB_HOME_ASSISTANT);
    lv_obj_t *system_page = make_page(s_ui.root, TAB_SYSTEM);

    build_display_page(display_page);
    build_network_page(network_page);
    build_ha_page(ha_page);
    build_system_page(system_page);
    build_editor(s_ui.root);

    for (int i = 0; i < TAB_COUNT; i++) {
        button_style(&s_ui.tabs[i], (settings_tab_t)i == TAB_DISPLAY,
                     COLOR_CYAN);
        if ((settings_tab_t)i != TAB_DISPLAY) {
            lv_obj_add_flag(s_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    refresh_display_values();
    refresh_all_fields();
    refresh_timezone_buttons();
    refresh_wifi_status();
    refresh_scan_ui();
    update_temperature();
    update_local_clock();

    s_timer = lv_timer_create(settings_timer_cb, TIMER_PERIOD_MS, NULL);
    if (s_timer == NULL) {
        memset(&s_ui, 0, sizeof(s_ui));
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "mounted");
    return ESP_OK;
}

void settings_unmount(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    /* Do not leave secrets in the textarea while the parent cleans up. */
    if (s_ui.editor.textarea != NULL) {
        lv_textarea_set_text(s_ui.editor.textarea, "");
    }

    memset(&s_ui, 0, sizeof(s_ui));
    ESP_LOGI(TAG, "unmounted");
}

const app_desc_t settings_app = {
    .id = "settings",
    .title = "Settings",
    .description = "Display, audio, network, Home Assistant and system.",
    .mount = settings_mount,
    .unmount = settings_unmount,
};
