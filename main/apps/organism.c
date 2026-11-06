#include "apps.h"

#include "ui_theme.h"

static esp_err_t organism_mount(lv_obj_t *parent)
{
    ui_theme_label(parent, "ORGANISM", &lv_font_montserrat_28, 0xE2E8F0, 60, 32);
    ui_theme_label(parent, "Generative visuals driven by the audio feature stream",
                   &lv_font_montserrat_14, 0x94A3B8, 62, 70);

    lv_obj_t *stage = ui_theme_card(parent, 60, 120, 904, 430);
    lv_obj_set_style_bg_color(stage, lv_color_hex(0x0F141B), 0);
    lv_obj_set_style_border_color(stage, lv_color_hex(0x3B1F4A), 0);

    lv_obj_t *orb = lv_obj_create(stage);
    lv_obj_set_size(orb, 180, 180);
    lv_obj_center(orb);
    lv_obj_remove_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(orb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb, lv_color_hex(0xE879F9), 0);
    lv_obj_set_style_bg_opa(orb, LV_OPA_20, 0);
    lv_obj_set_style_border_width(orb, 2, 0);
    lv_obj_set_style_border_color(orb, lv_color_hex(0xE879F9), 0);

    lv_obj_t *m8 = lv_label_create(orb);
    lv_label_set_text(m8, "M8");
    lv_obj_set_style_text_font(m8, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(m8, lv_color_hex(0xE879F9), 0);
    lv_obj_center(m8);

    ui_theme_label(stage,
                   "The organism engine will render into a double-buffered RGB565 canvas\n"
                   "and stay independent of LVGL. The audio service and dashboard come first.",
                   &lv_font_montserrat_14, 0x94A3B8, 24, 352);

    return ESP_OK;
}

const app_desc_t organism_app = {
    .id = "organism",
    .title = "Organism",
    .description = "Generative visuals driven by sound. Arrives in M8.",
    .mount = organism_mount,
    .unmount = NULL,
};
