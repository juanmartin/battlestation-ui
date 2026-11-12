#include "settings_codec.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_support.h"

static void test_defaults(void)
{
    app_settings_t s;
    settings_codec_defaults(&s);

    CHECK(s.brightness == 60);
    CHECK(s.mic_gain_db == 24);
    CHECK(s.output_volume == 60);
    CHECK_NEAR(s.a4_hz, 440.0, 0.001);
    CHECK(strcmp(s.timezone, "<-03>3") == 0);
    CHECK(s.wifi_ssid[0] == '\0');
    CHECK(s.ha_url[0] == '\0');
    CHECK(s.weather_entity[0] == '\0');
}

static void test_validation(void)
{
    app_settings_t s;
    settings_codec_defaults(&s);

    s.brightness = -100;
    s.mic_gain_db = -5;
    s.output_volume = 500;
    s.a4_hz = NAN;
    settings_codec_validate(&s);
    CHECK(s.brightness == 10);
    CHECK(s.mic_gain_db == 0);
    CHECK(s.output_volume == 100);
    CHECK_NEAR(s.a4_hz, 440.0, 0.001);

    s.brightness = 500;
    s.mic_gain_db = 100;
    s.output_volume = -1;
    s.a4_hz = 500.0f;
    settings_codec_validate(&s);
    CHECK(s.brightness == 100);
    CHECK(s.mic_gain_db == 37);
    CHECK(s.output_volume == 0);
    CHECK_NEAR(s.a4_hz, 450.0, 0.001);

    s.a4_hz = 400.0f;
    settings_codec_validate(&s);
    CHECK_NEAR(s.a4_hz, 430.0, 0.001);

    memset(s.wifi_ssid, 'A', sizeof(s.wifi_ssid));
    memset(s.timezone, 'B', sizeof(s.timezone));
    s.timezone[0] = '\0';
    settings_codec_validate(&s);
    CHECK(s.wifi_ssid[sizeof(s.wifi_ssid) - 1] == '\0');
    CHECK(strcmp(s.timezone, "<-03>3") == 0);

    memset(s.ha_url, 'C', sizeof(s.ha_url));
    settings_codec_validate(&s);
    CHECK(s.ha_url[sizeof(s.ha_url) - 1] == '\0');
}

static void test_roundtrip(void)
{
    app_settings_t in;
    app_settings_t out;
    uint8_t blob[sizeof(settings_codec_header_t) + sizeof(app_settings_t) + 4u];

    settings_codec_defaults(&in);
    in.brightness = 73;
    in.mic_gain_db = 12;
    in.output_volume = 41;
    in.a4_hz = 442.5f;
    strcpy(in.wifi_ssid, "Battlestation");
    strcpy(in.wifi_password, "secret");
    strcpy(in.ha_url, "http://192.168.1.10:8123");
    strcpy(in.ha_token, "token-not-logged");
    strcpy(in.timezone, "<-03>3");
    strcpy(in.light_entity[0], "light.one");
    strcpy(in.light_entity[3], "light.four");
    strcpy(in.weather_entity, "weather.home");

    size_t length = settings_codec_serialize(&in, blob, sizeof(blob));
    CHECK(length > 0);
    CHECK(length <= sizeof(blob));

    memset(&out, 0, sizeof(out));
    CHECK(settings_codec_deserialize(&out, blob, length));
    CHECK(out.brightness == 73);
    CHECK(out.mic_gain_db == 12);
    CHECK(out.output_volume == 41);
    CHECK_NEAR(out.a4_hz, 442.5, 0.001);
    CHECK(strcmp(out.wifi_ssid, "Battlestation") == 0);
    CHECK(strcmp(out.wifi_password, "secret") == 0);
    CHECK(strcmp(out.ha_url, "http://192.168.1.10:8123") == 0);
    CHECK(strcmp(out.light_entity[0], "light.one") == 0);
    CHECK(strcmp(out.light_entity[3], "light.four") == 0);
    CHECK(strcmp(out.weather_entity, "weather.home") == 0);

    CHECK(settings_codec_serialize(&in, blob, length - 1) == 0);
}

static void test_corruption(void)
{
    app_settings_t in;
    app_settings_t out;
    uint8_t blob[sizeof(settings_codec_header_t) + sizeof(app_settings_t) + 4u];

    settings_codec_defaults(&in);
    size_t length = settings_codec_serialize(&in, blob, sizeof(blob));
    CHECK(length > 0);

    uint8_t copy[sizeof(blob)];
    memcpy(copy, blob, length);
    CHECK(settings_codec_deserialize(&out, copy, length));

    memcpy(copy, blob, length);
    copy[length - 1] ^= 0x5Au;
    CHECK(!settings_codec_deserialize(&out, copy, length));

    memcpy(copy, blob, length);
    copy[0] ^= 0xFFu;
    CHECK(!settings_codec_deserialize(&out, copy, length));

    memcpy(copy, blob, length);
    copy[4] ^= 0xFFu;
    CHECK(!settings_codec_deserialize(&out, copy, length));

    memcpy(copy, blob, length);
    copy[6] ^= 0xFFu;
    CHECK(!settings_codec_deserialize(&out, copy, length));

    CHECK(!settings_codec_deserialize(&out, blob, length - 1));
    CHECK(!settings_codec_deserialize(&out, blob, length + 1));
    CHECK(!settings_codec_deserialize(&out, NULL, length));
    CHECK(!settings_codec_deserialize(NULL, blob, length));
}

int main(void)
{
    test_defaults();
    test_validation();
    test_roundtrip();
    test_corruption();
    return test_report("test_settings");
}
