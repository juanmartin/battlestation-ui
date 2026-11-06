idf_component_register(
    SRCS
        "main.c"
        "app_manager.c"
        "ui_theme.c"
        "ui_probe.c"
        "services/audio_service.c"
        "services/audio_analysis.c"
        "engine/audio_ring.c"
        "engine/audio_features.c"
        "wifi_probe.c"
        "diag_probe.c"
        "apps/launcher.c"
        "apps/dashboard.c"
        "apps/organism.c"
        "apps/settings.c"
    INCLUDE_DIRS "." "apps" "services" "engine"
    REQUIRES esp32_p4_wifi6_touch_lcd_7b lvgl__lvgl
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_driver_tsens
)
