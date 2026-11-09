#include "apps.h"

#include "ui_theme.h"

static void tile_clicked_cb(lv_event_t *e)
{
    const app_desc_t *app = (const app_desc_t *)lv_event_get_user_data(e);
    if (app != NULL) {
        app_manager_request(app->id);
    }
}

static void make_tile(lv_obj_t *parent, const app_desc_t *app, int x, int y,
                      uint32_t accent, const char *glyph)
{
    lv_obj_t *tile = ui_theme_card(parent, x, y, 280, 300);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1B2430), LV_STATE_PRESSED);

    lv_obj_t *badge = lv_obj_create(tile);
    lv_obj_set_pos(badge, 24, 24);
    lv_obj_set_size(badge, 64, 64);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(badge, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, 16, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);

    lv_obj_t *glyph_label = lv_label_create(badge);
    lv_label_set_text(glyph_label, glyph);
    lv_obj_set_style_text_font(glyph_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(glyph_label, lv_color_hex(0x0B0F14), 0);
    lv_obj_center(glyph_label);

    ui_theme_label(tile, app->title, &lv_font_montserrat_28, 0xE2E8F0, 24, 110);

    lv_obj_t *desc = ui_theme_label(tile, app->description, &lv_font_montserrat_14,
                                    0x94A3B8, 24, 154);
    lv_obj_set_width(desc, 232);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);

    ui_theme_label(tile, "OPEN", &lv_font_montserrat_14, accent, 24, 248);

    lv_obj_add_event_cb(tile, tile_clicked_cb, LV_EVENT_CLICKED, (void *)app);
}

static esp_err_t launcher_mount(lv_obj_t *parent)
{
    ui_theme_label(parent, "BATTLESTATION", &lv_font_montserrat_28, 0xE2E8F0, 60, 32);
    ui_theme_label(parent, "ESP32-P4  /  ESP32-C6  /  1024 x 600  /  launcher",
                   &lv_font_montserrat_14, 0x94A3B8, 62, 70);

    make_tile(parent, &dashboard_app, 60, 150, 0x22D3EE, "D");
    make_tile(parent, &organism_app, 372, 150, 0xE879F9, "O");
    make_tile(parent, &settings_app, 684, 150, 0x4ADE80, "S");

    lv_obj_t *footer = ui_theme_card(parent, 60, 490, 904, 72);
    ui_theme_label(footer, "Tap a tile to open an app.  HOME returns here from anywhere.",
                   &lv_font_montserrat_14, 0x94A3B8, 20, 12);
    ui_theme_label(footer, "M5 launcher shell  |  5 static apps  |  hardware path unchanged",
                   &lv_font_montserrat_14, 0x64748B, 20, 40);

    return ESP_OK;
}

const app_desc_t launcher_app = {
    .id = "launcher",
    .title = "Launcher",
    .description = "Home grid for the Battlestation apps.",
    .mount = launcher_mount,
    .unmount = NULL,
};
