#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_WIFI_SSID_MAX 33
#define SETTINGS_WIFI_PASS_MAX 65
#define SETTINGS_HA_URL_MAX 96
#define SETTINGS_HA_TOKEN_MAX 192
#define SETTINGS_ENTITY_MAX 64
#define SETTINGS_TIMEZONE_MAX 40
#define SETTINGS_LIGHT_COUNT 4

#define SETTINGS_MAGIC 0x42535431u /* "BST1" */
#define SETTINGS_VERSION 1u

typedef struct {
    int brightness;     /* 10..100 % */
    int mic_gain_db;    /* 0..37 dB */
    int output_volume;  /* 0..100 % */
    float a4_hz;        /* 430..450 Hz */
    char wifi_ssid[SETTINGS_WIFI_SSID_MAX];
    char wifi_password[SETTINGS_WIFI_PASS_MAX];
    char ha_url[SETTINGS_HA_URL_MAX];
    char ha_token[SETTINGS_HA_TOKEN_MAX];
    char timezone[SETTINGS_TIMEZONE_MAX];
    char light_entity[SETTINGS_LIGHT_COUNT][SETTINGS_ENTITY_MAX];
    char weather_entity[SETTINGS_ENTITY_MAX];
} app_settings_t;

/*
 * Pure-C codec and validation. No NVS/ESP-IDF dependency so the same rules can
 * be host-tested. The NVS service in services/settings.c wraps this with a
 * single versioned blob.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
} settings_codec_header_t;

void settings_codec_defaults(app_settings_t *out);
void settings_codec_validate(app_settings_t *settings);

/* Returns the serialized length, or 0 when the buffer is too small. */
size_t settings_codec_serialize(const app_settings_t *settings, uint8_t *out,
                                size_t capacity);

/* Returns false for a short, corrupt, wrong-version or wrong-size blob. On
 * failure the caller should keep its previous settings/defaults. */
bool settings_codec_deserialize(app_settings_t *settings, const uint8_t *data,
                                size_t length);
