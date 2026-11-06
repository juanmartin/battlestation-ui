#include "apps.h"

#include <stdio.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "sdkconfig.h"
#include "ui_theme.h"

static int s_brightness = CONFIG_APP_BACKLIGHT_PERCENT;

static void brightness_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
    int value = (int)lv_slider_get_value(slider);

    s_brightness = value;
    bsp_display_brightness_set(value);
    if (value_label != NULL) {
        lv_label_set_text_fmt(value_label, "%d%%", value);
    }
}

static esp_err_t settings_mount(lv_obj_t *parent)
{
    ui_theme_label(parent, "SETTINGS", &lv_font_montserrat_28, 0xE2E8F0, 60, 32);
    ui_theme_label(parent, "Device controls available in the M5 shell",
                   &lv_font_montserrat_14, 0x94A3B8, 62, 70);

    lv_obj_t *display = ui_theme_card(parent, 60, 120, 440, 300);
    ui_theme_label(display, "DISPLAY", &lv_font_montserrat_20, 0x22D3EE, 20, 16);
    ui_theme_label(display, "Backlight brightness", &lv_font_montserrat_14,
                   0x94A3B8, 20, 52);

    lv_obj_t *value_label = ui_theme_label(display, "", &lv_font_montserrat_20,
                                           0xE2E8F0, 348, 48);
    lv_label_set_text_fmt(value_label, "%d%%", s_brightness);

    lv_obj_t *slider = lv_slider_create(display);
    lv_obj_set_pos(slider, 20, 92);
    lv_obj_set_size(slider, 400, 20);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, s_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x0B0F14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x22D3EE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xE2E8F0), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED,
                        value_label);

    ui_theme_label(display,
                   "Brightness applies immediately. Persistence and\n"
                   "provisioning arrive in M9.",
                   &lv_font_montserrat_14, 0x64748B, 20, 150);

    lv_obj_t *device = ui_theme_card(parent, 532, 120, 432, 300);
    ui_theme_label(device, "DEVICE", &lv_font_montserrat_20, 0x4ADE80, 20, 16);
    ui_theme_label(device, "Waveshare ESP32-P4-WIFI6-Touch-LCD-7B",
                   &lv_font_montserrat_14, 0xE2E8F0, 20, 56);
    ui_theme_label(device, "ESP32-P4 rev1.3  /  32 MB flash  /  32 MB PSRAM",
                   &lv_font_montserrat_14, 0x94A3B8, 20, 84);
    ui_theme_label(device, "Display: EK79007 1024 x 600  /  rotation 180",
                   &lv_font_montserrat_14, 0x94A3B8, 20, 112);
    ui_theme_label(device, "Touch: GT911  /  Wi-Fi: ESP32-C6 over SDIO",
                   &lv_font_montserrat_14, 0x94A3B8, 20, 140);

    char mic[64];
    snprintf(mic, sizeof(mic), "Microphone PGA: %d dB  /  16 kHz stereo",
             CONFIG_APP_MIC_GAIN_DB);
    ui_theme_label(device, mic, &lv_font_montserrat_14, 0x94A3B8, 20, 168);

    ui_theme_label(device, "Firmware: M5 launcher shell",
                   &lv_font_montserrat_14, 0x64748B, 20, 220);

    lv_obj_t *footer = ui_theme_card(parent, 60, 450, 904, 100);
    ui_theme_label(footer, "SETTINGS ARE INTENTIONALLY MINIMAL",
                   &lv_font_montserrat_20, 0x4ADE80, 20, 14);
    ui_theme_label(footer,
                   "Brightness is live. Wi-Fi credentials stay in menuconfig until the\n"
                   "provisioning flow lands in M9. Audio gain remains a build-time setting.",
                   &lv_font_montserrat_14, 0x94A3B8, 20, 46);

    return ESP_OK;
}

const app_desc_t settings_app = {
    .id = "settings",
    .title = "Settings",
    .description = "Brightness and device information.",
    .mount = settings_mount,
    .unmount = NULL,
};
