#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

/* A statically linked application. The manager owns exactly one mounted app at
 * a time. mount() creates its LVGL tree under parent; unmount() releases any
 * timers or non-LVGL resources. The manager deletes the LVGL tree afterwards. */
typedef struct app_desc {
    const char *id;
    const char *title;
    const char *description;
    esp_err_t (*mount)(lv_obj_t *parent);
    void (*unmount)(void);
} app_desc_t;

/* Initialize the root screen and mount the launcher. */
esp_err_t app_manager_start(lv_display_t *disp);

/* Immediate switch. Prefer app_manager_request() from LVGL event callbacks so
 * the event target is not deleted while its callback is running. */
esp_err_t app_manager_switch(const char *id);

/* Defer a switch to the manager timer (safe inside LVGL events). */
esp_err_t app_manager_request(const char *id);

const char *app_manager_current(void);
size_t app_manager_count(void);
size_t app_manager_child_count(void);

/* Diagnostics owned by the manager so they keep running across app switches. */
uint32_t app_manager_heartbeat(void);
uint32_t app_manager_fps(void);
