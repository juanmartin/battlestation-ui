#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t ui_probe_start(lv_display_t *disp);
uint32_t ui_probe_heartbeat(void);
uint32_t ui_probe_fps(void);
