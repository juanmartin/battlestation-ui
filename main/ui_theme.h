#pragma once

#include "lvgl.h"

lv_obj_t *ui_theme_card(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *ui_theme_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                         uint32_t color, int x, int y);
