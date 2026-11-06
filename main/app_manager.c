#include "app_manager.h"

#include <stdbool.h>
#include <string.h>

#include "apps/apps.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "app_manager";

static lv_display_t *s_disp;
static lv_obj_t *s_root;
static lv_obj_t *s_content;
static lv_timer_t *s_timer;
static const app_desc_t *s_current;
static const char *s_pending_id;
static volatile uint32_t s_heartbeat;
static volatile uint32_t s_fps;
static bool s_fps_enabled;

static const app_desc_t *const s_apps[] = {
    &launcher_app,
    &dashboard_app,
    &organism_app,
    &settings_app,
};

static const app_desc_t *find_app(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_apps) / sizeof(s_apps[0]); i++) {
        if (s_apps[i]->id != NULL && strcmp(s_apps[i]->id, id) == 0) {
            return s_apps[i];
        }
    }
    return NULL;
}

static void home_clicked_cb(lv_event_t *e)
{
    (void)e;
    app_manager_request("launcher");
}

static void add_home_button(void)
{
    if (s_current == &launcher_app) {
        return;
    }

    lv_obj_t *btn = lv_obj_create(s_content);
    lv_obj_set_pos(btn, BSP_LCD_H_RES - 32 - 78, 16);
    lv_obj_set_size(btn, 78, 36);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x151B23), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x2A3542), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "HOME");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE2E8F0), 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, home_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(btn);
}

static esp_err_t switch_locked(const app_desc_t *app)
{
    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_current == app) {
        return ESP_OK;
    }

    if (s_current != NULL && s_current->unmount != NULL) {
        s_current->unmount();
    }
    lv_obj_clean(s_content);
    s_current = app;

    ESP_LOGI(TAG, "mount '%s'", app->id);
    esp_err_t err = app->mount(s_content);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount '%s' failed: %s", app->id, esp_err_to_name(err));
        if (app->unmount != NULL) {
            app->unmount();
        }
        s_current = NULL;
        lv_obj_clean(s_content);
        lv_obj_t *label = lv_label_create(s_content);
        lv_label_set_text(label, "App failed to start");
        lv_obj_set_style_text_color(label, lv_color_hex(0xF87171), 0);
        lv_obj_center(label);
        return err;
    }

    add_home_button();
    return ESP_OK;
}

static void manager_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_heartbeat++;

    if (s_disp != NULL) {
        uint32_t fps = 0;
        if (s_fps_enabled && esp_lv_adapter_get_fps(s_disp, &fps) == ESP_OK) {
            s_fps = fps;
        }
    }

    if (s_pending_id != NULL) {
        const char *id = s_pending_id;
        s_pending_id = NULL;
        const app_desc_t *app = find_app(id);
        if (app != NULL && app != s_current) {
            switch_locked(app);
        }
    }
}

#if CONFIG_APP_SWITCH_SELFTEST
static void selftest_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2000));

    const size_t baseline = app_manager_child_count();
    const unsigned int0 = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const unsigned psram0 = (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const char *cycle[] = {"dashboard", "organism", "settings", "launcher"};
    int switches = 0;

    ESP_LOGI(TAG, "selftest start: children=%u int=%u psram=%u",
             (unsigned)baseline, int0, psram0);

    for (int i = 0; i < 25; i++) {
        for (size_t j = 0; j < sizeof(cycle) / sizeof(cycle[0]); j++) {
            esp_err_t err = app_manager_switch(cycle[j]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "selftest switch '%s' failed: %s",
                         cycle[j], esp_err_to_name(err));
            }
            switches++;
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        ESP_LOGI(TAG, "selftest cycle=%d switches=%d children=%u int=%u psram=%u",
                 i + 1, switches, (unsigned)app_manager_child_count(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    const unsigned int1 = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const unsigned psram1 = (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t final_children = app_manager_child_count();

    ESP_LOGI(TAG,
             "selftest done: switches=%d children=%u baseline=%u child_delta=%d "
             "int_delta=%d psram_delta=%d",
             switches, (unsigned)final_children, (unsigned)baseline,
             (int)final_children - (int)baseline,
             (int)int0 - (int)int1, (int)psram0 - (int)psram1);

    vTaskDelete(NULL);
}
#endif

esp_err_t app_manager_start(lv_display_t *disp)
{
    if (disp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_disp = disp;
    s_fps_enabled = (esp_lv_adapter_fps_stats_enable(disp, true) == ESP_OK);
    if (!s_fps_enabled) {
        ESP_LOGW(TAG, "FPS statistics not available");
    }

    if (!bsp_display_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }

    s_root = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_size(s_root, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x0B0F14), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);

    s_content = lv_obj_create(s_root);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_radius(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);

    esp_err_t err = switch_locked(find_app("launcher"));
    if (err == ESP_OK) {
        s_timer = lv_timer_create(manager_timer_cb, 50, NULL);
    }

    bsp_display_unlock();

#if CONFIG_APP_SWITCH_SELFTEST
    if (err == ESP_OK) {
        xTaskCreate(selftest_task, "m5_selftest", 4096, NULL, 2, NULL);
    }
#endif

    return err;
}

esp_err_t app_manager_switch(const char *id)
{
    const app_desc_t *app = find_app(id);
    if (app == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_content == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bsp_display_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = switch_locked(app);
    bsp_display_unlock();
    return err;
}

esp_err_t app_manager_request(const char *id)
{
    if (find_app(id) == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    s_pending_id = id;
    return ESP_OK;
}

const char *app_manager_current(void)
{
    return s_current != NULL ? s_current->id : "";
}

size_t app_manager_count(void)
{
    return sizeof(s_apps) / sizeof(s_apps[0]);
}

size_t app_manager_child_count(void)
{
    return s_content != NULL ? lv_obj_get_child_count(s_content) : 0;
}

uint32_t app_manager_heartbeat(void)
{
    return s_heartbeat;
}

uint32_t app_manager_fps(void)
{
    return s_fps;
}
