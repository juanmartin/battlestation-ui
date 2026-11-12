#include "settings_codec.h"

#include <math.h>
#include <string.h>

#define SETTINGS_CRC_BYTES 4u
#define SETTINGS_DEFAULT_BRIGHTNESS 60
#define SETTINGS_DEFAULT_MIC_GAIN 24
#define SETTINGS_DEFAULT_VOLUME 60
#define SETTINGS_DEFAULT_A4 440.0f
#define SETTINGS_DEFAULT_TIMEZONE "<-03>3"

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static int clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void ensure_terminated(char *text, size_t size)
{
    if (size > 0u) {
        text[size - 1u] = '\0';
    }
}

static void copy_settings(app_settings_t *dst, const app_settings_t *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->brightness = src->brightness;
    dst->mic_gain_db = src->mic_gain_db;
    dst->output_volume = src->output_volume;
    dst->a4_hz = src->a4_hz;
    memcpy(dst->wifi_ssid, src->wifi_ssid, sizeof(dst->wifi_ssid));
    memcpy(dst->wifi_password, src->wifi_password, sizeof(dst->wifi_password));
    memcpy(dst->ha_url, src->ha_url, sizeof(dst->ha_url));
    memcpy(dst->ha_token, src->ha_token, sizeof(dst->ha_token));
    memcpy(dst->timezone, src->timezone, sizeof(dst->timezone));
    for (size_t i = 0; i < SETTINGS_LIGHT_COUNT; i++) {
        memcpy(dst->light_entity[i], src->light_entity[i],
               sizeof(dst->light_entity[i]));
    }
    memcpy(dst->weather_entity, src->weather_entity,
           sizeof(dst->weather_entity));
}

void settings_codec_defaults(app_settings_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->brightness = SETTINGS_DEFAULT_BRIGHTNESS;
    out->mic_gain_db = SETTINGS_DEFAULT_MIC_GAIN;
    out->output_volume = SETTINGS_DEFAULT_VOLUME;
    out->a4_hz = SETTINGS_DEFAULT_A4;
    memcpy(out->timezone, SETTINGS_DEFAULT_TIMEZONE,
           sizeof(SETTINGS_DEFAULT_TIMEZONE));
}

void settings_codec_validate(app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    settings->brightness = clamp_int(settings->brightness, 10, 100);
    settings->mic_gain_db = clamp_int(settings->mic_gain_db, 0, 37);
    settings->output_volume = clamp_int(settings->output_volume, 0, 100);

    if (!isfinite(settings->a4_hz)) {
        settings->a4_hz = SETTINGS_DEFAULT_A4;
    }
    if (settings->a4_hz < 430.0f) {
        settings->a4_hz = 430.0f;
    } else if (settings->a4_hz > 450.0f) {
        settings->a4_hz = 450.0f;
    }

    ensure_terminated(settings->wifi_ssid, sizeof(settings->wifi_ssid));
    ensure_terminated(settings->wifi_password, sizeof(settings->wifi_password));
    ensure_terminated(settings->ha_url, sizeof(settings->ha_url));
    ensure_terminated(settings->ha_token, sizeof(settings->ha_token));
    ensure_terminated(settings->timezone, sizeof(settings->timezone));
    for (size_t i = 0; i < SETTINGS_LIGHT_COUNT; i++) {
        ensure_terminated(settings->light_entity[i],
                          sizeof(settings->light_entity[i]));
    }
    ensure_terminated(settings->weather_entity, sizeof(settings->weather_entity));

    if (settings->timezone[0] == '\0') {
        memcpy(settings->timezone, SETTINGS_DEFAULT_TIMEZONE,
               sizeof(SETTINGS_DEFAULT_TIMEZONE));
    }
}

size_t settings_codec_serialize(const app_settings_t *settings, uint8_t *out,
                                size_t capacity)
{
    if (settings == NULL || out == NULL) {
        return 0;
    }

    const size_t payload = sizeof(app_settings_t);
    const size_t total = sizeof(settings_codec_header_t) + payload +
                         SETTINGS_CRC_BYTES;
    if (capacity < total) {
        return 0;
    }

    app_settings_t clean;
    copy_settings(&clean, settings);
    settings_codec_validate(&clean);

    settings_codec_header_t header = {
        .magic = SETTINGS_MAGIC,
        .version = SETTINGS_VERSION,
        .size = (uint16_t)payload,
    };

    size_t offset = 0;
    memcpy(out + offset, &header, sizeof(header));
    offset += sizeof(header);
    memcpy(out + offset, &clean, payload);
    offset += payload;
    uint32_t crc = crc32_bytes(out, offset);
    memcpy(out + offset, &crc, sizeof(crc));
    offset += sizeof(crc);
    return offset;
}

bool settings_codec_deserialize(app_settings_t *settings, const uint8_t *data,
                                size_t length)
{
    if (settings == NULL || data == NULL ||
        length < sizeof(settings_codec_header_t) + sizeof(app_settings_t) +
                     SETTINGS_CRC_BYTES) {
        return false;
    }

    settings_codec_header_t header;
    memcpy(&header, data, sizeof(header));
    if (header.magic != SETTINGS_MAGIC || header.version != SETTINGS_VERSION ||
        header.size != sizeof(app_settings_t)) {
        return false;
    }

    const size_t payload_len = sizeof(settings_codec_header_t) + header.size;
    if (length != payload_len + SETTINGS_CRC_BYTES) {
        return false;
    }

    uint32_t stored_crc = 0;
    memcpy(&stored_crc, data + payload_len, sizeof(stored_crc));
    uint32_t computed_crc = crc32_bytes(data, payload_len);
    if (stored_crc != computed_crc) {
        return false;
    }

    app_settings_t decoded;
    memcpy(&decoded, data + sizeof(settings_codec_header_t), header.size);
    settings_codec_validate(&decoded);
    *settings = decoded;
    return true;
}
