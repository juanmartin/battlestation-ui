idf_component_register(
    SRCS "main.c" "ui_probe.c" "audio_probe.c" "wifi_probe.c" "diag_probe.c"
    INCLUDE_DIRS "."
    REQUIRES esp32_p4_wifi6_touch_lcd_7b lvgl__lvgl
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_driver_tsens
)
