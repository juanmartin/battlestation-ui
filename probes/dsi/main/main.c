#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#define STRIPE_H 50
#define STRIPE_BYTES (BSP_LCD_H_RES * STRIPE_H * sizeof(uint16_t))

static const char *TAG = "dsi_probe";
static SemaphoreHandle_t s_done;

static bool IRAM_ATTR on_color_done(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *edata,
                                    void *ctx)
{
    (void)panel;
    (void)edata;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &hp);
    return hp == pdTRUE;
}

void app_main(void)
{
    bsp_lcd_handles_t handles;
    ESP_ERROR_CHECK(bsp_display_new_with_handles(NULL, &handles));
    ESP_ERROR_CHECK(bsp_display_brightness_set(60));

    s_done = xSemaphoreCreateBinary();
    configASSERT(s_done);
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_color_done,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(handles.panel, &cbs, s_done));

    uint16_t *buf = heap_caps_malloc(STRIPE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (buf == NULL) {
        buf = heap_caps_malloc(STRIPE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    ESP_LOGI(TAG, "buf=%p bytes=%u internal=%u psram=%u",
             buf, (unsigned)STRIPE_BYTES,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    uint32_t frame = 0;
    uint32_t timeouts = 0;
    int64_t start = esp_timer_get_time();

    while (true) {
        for (int y = 0; y < BSP_LCD_V_RES; y += STRIPE_H) {
            uint16_t base = (uint16_t)((frame * 37 + y * 3) & 0xFFFF);
            for (int row = 0; row < STRIPE_H; row++) {
                uint16_t color = (uint16_t)(base + row * 11);
                uint16_t *dst = buf + row * BSP_LCD_H_RES;
                for (int x = 0; x < BSP_LCD_H_RES; x++) {
                    dst[x] = (uint16_t)(color ^ (x * 3));
                }
            }

            xSemaphoreTake(s_done, 0);
            esp_err_t ret = esp_lcd_panel_draw_bitmap(handles.panel, 0, y,
                                                      BSP_LCD_H_RES, y + STRIPE_H, buf);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "draw y=%d failed: %s", y, esp_err_to_name(ret));
            }
            if (xSemaphoreTake(s_done, pdMS_TO_TICKS(100)) != pdTRUE) {
                timeouts++;
                if ((timeouts % 10) == 1) {
                    ESP_LOGW(TAG, "frame=%" PRIu32 " stripe y=%d completion TIMEOUT (total=%" PRIu32 ")",
                             frame, y, timeouts);
                }
            }
        }
        frame++;
        if ((frame % 30) == 0) {
            int64_t elapsed = esp_timer_get_time() - start;
            ESP_LOGI(TAG, "frame=%" PRIu32 " fps=%.1f timeouts=%" PRIu32 " int=%u psram=%u",
                     frame, frame * 1000000.0f / (float)elapsed, timeouts,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }
}
