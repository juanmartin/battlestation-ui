#include "metronome.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "test_support.h"

#define TEST_MAX_EVENTS 512u
#define TEST_STEREO_FRAMES 32000u

typedef struct {
    uint64_t sample;
    uint8_t beat;
    bool accent;
} beat_event_t;

typedef struct {
    beat_event_t events[TEST_MAX_EVENTS];
    size_t count;
    size_t overflow;
} recorder_t;

static int16_t g_stereo[TEST_STEREO_FRAMES * 2u];
static int16_t g_mono[TEST_STEREO_FRAMES];

static void record_beat(void *user, uint64_t sample_index, uint8_t beat,
                        bool accent)
{
    recorder_t *recorder = user;
    if (recorder->count < TEST_MAX_EVENTS) {
        recorder->events[recorder->count].sample = sample_index;
        recorder->events[recorder->count].beat = beat;
        recorder->events[recorder->count].accent = accent;
        recorder->count++;
    } else {
        recorder->overflow++;
    }
}

static void render_blocks(metronome_t *engine, uint32_t sample_rate,
                          double seconds, size_t block)
{
    int16_t buffer[512];
    size_t total = (size_t)lrint((double)sample_rate * seconds);
    size_t done = 0;
    while (done < total) {
        size_t count = total - done;
        if (count > block) {
            count = block;
        }
        if (count > sizeof(buffer) / (2u * sizeof(buffer[0]))) {
            count = sizeof(buffer) / (2u * sizeof(buffer[0]));
        }
        metronome_render(engine, buffer, count);
        done += count;
    }
}

static void check_timing(float bpm, metronome_meter_t meter)
{
    const uint32_t sample_rate = 48000u;
    metronome_t engine;
    metronome_config_t cfg = {0};
    cfg.sample_rate = sample_rate;
    cfg.bpm = bpm;
    cfg.meter = meter;
    cfg.accent_beat = 1;
    CHECK(metronome_init(&engine, &cfg));

    recorder_t recorder = {0};
    metronome_set_beat_callback(&engine, record_beat, &recorder);

    render_blocks(&engine, sample_rate, 60.0, 256u);

    uint32_t beats_per_bar = (uint32_t)meter;
    uint32_t expected_count =
        (uint32_t)lrint((double)bpm * 60.0 / 60.0);
    CHECK(recorder.overflow == 0u);
    CHECK(recorder.count == expected_count);
    CHECK(metronome_beat_count(&engine) == expected_count);

    double ideal_period = (double)sample_rate * 60.0 / (double)bpm;
    double max_error = 0.0;
    for (size_t i = 1; i < recorder.count; i++) {
        double interval = (double)(recorder.events[i].sample -
                                   recorder.events[i - 1u].sample);
        double error = fabs(interval - ideal_period);
        if (error > max_error) {
            max_error = error;
        }
    }
    CHECK(max_error <= 1.0);

    for (size_t i = 0; i < recorder.count; i++) {
        uint8_t expected_beat = (uint8_t)(i % beats_per_bar + 1u);
        CHECK(recorder.events[i].beat == expected_beat);
        CHECK(recorder.events[i].accent == (expected_beat == 1u));
    }
}

static void test_timing_and_meters(void)
{
    check_timing(60.0f, METRONOME_METER_2_4);
    check_timing(120.0f, METRONOME_METER_4_4);
    check_timing(137.0f, METRONOME_METER_3_4);
    check_timing(240.0f, METRONOME_METER_6_8);
}

static void test_accent_beat_and_pattern(void)
{
    metronome_t engine;
    metronome_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.bpm = 240.0f;
    cfg.meter = METRONOME_METER_3_4;
    cfg.accent_beat = 3;
    CHECK(metronome_init(&engine, &cfg));

    recorder_t recorder = {0};
    metronome_set_beat_callback(&engine, record_beat, &recorder);
    render_blocks(&engine, 16000u, 2.0, 160u);

    CHECK(recorder.count == 8u);
    for (size_t i = 0; i < recorder.count; i++) {
        uint8_t expected_beat = (uint8_t)(i % 3u + 1u);
        CHECK(recorder.events[i].beat == expected_beat);
        CHECK(recorder.events[i].accent == (expected_beat == 3u));
    }
    CHECK(metronome_beat_in_bar(&engine) == 2u);
    CHECK(metronome_bar_count(&engine) == 2u);
}

static int peak_abs(const int16_t *stereo, size_t start, size_t end)
{
    int peak = 0;
    for (size_t i = start; i < end; i++) {
        int value = stereo[i * 2u];
        if (value < 0) {
            value = -value;
        }
        if (value > peak) {
            peak = value;
        }
    }
    return peak;
}

static void test_accent_audio_level(void)
{
    const uint32_t sample_rate = 16000u;
    metronome_t engine;
    metronome_config_t cfg = {0};
    cfg.sample_rate = sample_rate;
    cfg.bpm = 120.0f;
    cfg.meter = METRONOME_METER_4_4;
    cfg.accent_beat = 1;
    cfg.accent_frequency_hz = 200.0f;
    cfg.beat_frequency_hz = 150.0f;
    cfg.accent_gain = 0.9f;
    cfg.beat_gain = 0.4f;
    cfg.click_ms = 30.0f;
    cfg.attack_ms = 1.0f;
    CHECK(metronome_init(&engine, &cfg));

    recorder_t recorder = {0};
    metronome_set_beat_callback(&engine, record_beat, &recorder);

    size_t total = sample_rate * 2u;
    int16_t buffer[320];
    size_t done = 0;
    while (done < total) {
        size_t count = total - done;
        if (count > 160u) {
            count = 160u;
        }
        metronome_render(&engine, buffer, count);
        memcpy(g_stereo + done * 2u, buffer,
               count * 2u * sizeof(int16_t));
        done += count;
    }

    CHECK(recorder.count == 4u);
    size_t click_length = (size_t)lrintf(0.030f * (float)sample_rate);
    int accent_peak = peak_abs(g_stereo, recorder.events[0].sample,
                               recorder.events[0].sample + click_length);
    int normal_peak = peak_abs(g_stereo, recorder.events[1].sample,
                               recorder.events[1].sample + click_length);
    CHECK(accent_peak > 0);
    CHECK(normal_peak > 0);
    CHECK((double)accent_peak > (double)normal_peak * 1.5);

    CHECK(metronome_beat_count(&engine) == 4u);
    CHECK(metronome_beat_in_bar(&engine) == 4u);
    CHECK(metronome_bar_count(&engine) == 0u);
}

static void test_bpm_change_click_free(void)
{
    const uint32_t sample_rate = 16000u;
    metronome_t engine;
    metronome_config_t cfg = {0};
    cfg.sample_rate = sample_rate;
    cfg.bpm = 120.0f;
    cfg.meter = METRONOME_METER_4_4;
    cfg.accent_frequency_hz = 200.0f;
    cfg.beat_frequency_hz = 200.0f;
    cfg.accent_gain = 0.8f;
    cfg.beat_gain = 0.8f;
    CHECK(metronome_init(&engine, &cfg));

    recorder_t recorder = {0};
    metronome_set_beat_callback(&engine, record_beat, &recorder);

    int16_t buffer[320];
    int previous = 0;
    bool have_previous = false;
    int maximum_delta = 0;

    for (int phase = 0; phase < 2; phase++) {
        if (phase == 1) {
            metronome_set_bpm(&engine, 240.0f);
        }
        size_t total = sample_rate * 2u;
        size_t done = 0;
        while (done < total) {
            size_t count = total - done;
            if (count > 160u) {
                count = 160u;
            }
            metronome_render(&engine, buffer, count);
            for (size_t i = 0; i < count; i++) {
                int current = buffer[i * 2u];
                if (have_previous) {
                    int delta = current - previous;
                    if (delta < 0) {
                        delta = -delta;
                    }
                    if (delta > maximum_delta) {
                        maximum_delta = delta;
                    }
                }
                previous = current;
                have_previous = true;
            }
            done += count;
        }
    }

    CHECK(maximum_delta < 6000);
    CHECK(recorder.count >= 8u);
    for (size_t i = recorder.count - 3u; i < recorder.count; i++) {
        uint64_t interval =
            recorder.events[i].sample - recorder.events[i - 1u].sample;
        CHECK(interval >= 3999u && interval <= 4001u);
    }
}

static void test_mono_matches_stereo(void)
{
    metronome_t stereo_engine;
    metronome_t mono_engine;
    metronome_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.bpm = 100.0f;
    cfg.meter = METRONOME_METER_4_4;
    CHECK(metronome_init(&stereo_engine, &cfg));
    CHECK(metronome_init(&mono_engine, &cfg));

    size_t total = 16000u;
    size_t done = 0;
    int16_t stereo_block[320];
    while (done < total) {
        size_t count = total - done;
        if (count > 160u) {
            count = 160u;
        }
        metronome_render(&stereo_engine, stereo_block, count);
        metronome_render_mono(&mono_engine, g_mono + done, count);
        for (size_t i = 0; i < count; i++) {
            CHECK(stereo_block[i * 2u] == g_mono[done + i]);
            CHECK(stereo_block[i * 2u + 1u] == g_mono[done + i]);
        }
        done += count;
    }

    CHECK(metronome_beat_count(&stereo_engine) ==
          metronome_beat_count(&mono_engine));
}

static void test_configuration_and_nulls(void)
{
    metronome_t engine;
    metronome_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.bpm = 1000.0f;
    cfg.meter = METRONOME_METER_6_8;
    cfg.accent_beat = 9;
    CHECK(metronome_init(&engine, &cfg));
    CHECK(metronome_beat_in_bar(&engine) == 0u);
    CHECK(metronome_beat_count(&engine) == 0u);

    CHECK(!metronome_init(NULL, &cfg));
    CHECK(!metronome_init(&engine, NULL));
    CHECK(!metronome_set_click_wave(&engine, (metronome_click_wave_t)99));
    CHECK(metronome_set_click_wave(&engine, METRONOME_CLICK_SQUARE));
    metronome_set_bpm(&engine, 10.0f); /* clamped, must not crash */
    metronome_set_accent_beat(&engine, 0u);
    render_blocks(&engine, 16000u, 0.05, 160u);
    CHECK(metronome_beat_count(&engine) == 1u);

    metronome_render(NULL, NULL, 0u);
    metronome_render_mono(NULL, NULL, 0u);
    CHECK(metronome_beat_count(NULL) == 0u);
    CHECK(metronome_bar_count(NULL) == 0u);
    CHECK(metronome_beat_in_bar(NULL) == 0u);
}

int main(void)
{
    test_timing_and_meters();
    test_accent_beat_and_pattern();
    test_accent_audio_level();
    test_bpm_change_click_free();
    test_mono_matches_stereo();
    test_configuration_and_nulls();
    return test_report("test_metronome");
}
