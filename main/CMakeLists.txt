set(main_srcs
    "main.c"
    "app_manager.c"
    "ui_theme.c"
    "ui_probe.c"
    "services/audio_service.c"
    "services/audio_analysis.c"
    "services/time_service.c"
    "services/ha_client.c"
    "engine/audio_ring.c"
    "engine/audio_features.c"
    "engine/audio_fft.c"
    "engine/audio_mixer.c"
    "engine/organism_engine.c"
    "wifi_probe.c"
    "diag_probe.c"
    "apps/launcher.c"
    "apps/dashboard.c"
    "apps/organism.c"
    "apps/settings.c"
    "apps/diagnostics.c"
)

if(CONFIG_APP_AUDIO_TEST_TONE)
    list(APPEND main_srcs "services/audio_service_selftest.c")
endif()

if(CONFIG_APP_HA_TEST)
    list(APPEND main_srcs "services/ha_client_selftest.c")
endif()

idf_component_register(
    SRCS ${main_srcs}
    INCLUDE_DIRS "." "apps" "services" "engine"
    REQUIRES esp32_p4_wifi6_touch_lcd_7b lvgl__lvgl
    PRIV_REQUIRES esp_wifi esp_event esp_netif lwip nvs_flash esp_driver_tsens
                 esp_http_client json esp_websocket_client
)
