#include "tone_engine.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "test_support.h"

#define TEST_MAX_FRAMES 48000u
#define TEST_TWO_PI 6.283185307179586476925286766559

static int16_t g_stereo_a[TEST_MAX_FRAMES * 2u];
static int16_t g_stereo_b[TEST_MAX_FRAMES * 2u];

static double estimate_frequency(const int16_t *stereo, size_t frames,
                                 double sample_rate, size_t skip,
                                 size_t count)
{
    if (skip == 0) {
        skip = 1;
    }
    size_t end = skip + count;
    if (end > frames) {
        end = frames;
    }
    if (end <= skip + 1) {
        return 0.0;
    }

    int16_t previous = stereo[(skip - 1u) * 2u];
    double first = 0.0;
    double last = 0.0;
    size_t crossings = 0;
    for (size_t i = skip; i < end; i++) {
        int16_t current = stereo[i * 2u];
        if (previous <= 0 && current > 0) {
            double fraction = (double)(-previous) /
                              ((double)current - (double)previous);
            double position = (double)(i - 1u) + fraction;
            if (crossings == 0) {
                first = position;
            }
            last = position;
            crossings++;
        }
        previous = current;
    }
    if (crossings < 2) {
        return 0.0;
    }
    return (double)(crossings - 1u) * sample_rate / (last - first);
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

static double rms_left(const int16_t *stereo, size_t start, size_t end)
{
    double sum = 0.0;
    for (size_t i = start; i < end; i++) {
        double value = stereo[i * 2u];
        sum += value * value;
    }
    return sqrt(sum / (double)(end - start));
}

static void check_waveform(tone_wave_t waveform, double frequency,
                           uint32_t sample_rate)
{
    tone_engine_t engine;
    tone_engine_config_t cfg = {0};
    cfg.sample_rate = sample_rate;
    cfg.waveform = waveform;
    cfg.frequency_hz = (float)frequency;
    cfg.amplitude = 0.8f;
    cfg.ramp_ms = 5.0f;
    CHECK(tone_engine_init(&engine, &cfg));

    size_t frames = sample_rate;
    tone_engine_render(&engine, g_stereo_a, frames);

    size_t skip = sample_rate / 4u;
    size_t count = sample_rate / 2u;
    double estimated =
        estimate_frequency(g_stereo_a, frames, (double)sample_rate, skip,
                           count);
    CHECK_NEAR(estimated, frequency, frequency * 0.001);

    int peak = peak_abs(g_stereo_a, sample_rate / 5u, frames - 1u);
    CHECK_NEAR((double)peak / 32767.0, 0.8, 0.02);
}

static void test_all_waveforms(void)
{
    check_waveform(TONE_WAVE_SINE, 440.0, 16000u);
    check_waveform(TONE_WAVE_SQUARE, 440.0, 16000u);
    check_waveform(TONE_WAVE_TRIANGLE, 440.0, 16000u);
    check_waveform(TONE_WAVE_SAW, 440.0, 16000u);
    check_waveform(TONE_WAVE_SINE, 1000.0, 48000u);
}

static void test_amplitude(void)
{
    tone_engine_t engine;
    tone_engine_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.waveform = TONE_WAVE_SINE;
    cfg.frequency_hz = 440.0f;
    cfg.amplitude = 0.5f;
    cfg.ramp_ms = 5.0f;
    CHECK(tone_engine_init(&engine, &cfg));

    tone_engine_render(&engine, g_stereo_a, 8000u);
    int peak = peak_abs(g_stereo_a, 4000u, 8000u);
    CHECK_NEAR((double)peak / 32767.0, 0.5, 0.01);

    double rms = rms_left(g_stereo_a, 4000u, 8000u);
    double expected_rms = 0.5 * 32767.0 / sqrt(2.0);
    CHECK_NEAR(rms, expected_rms, expected_rms * 0.01);

    tone_engine_set_amplitude(&engine, 0.0f);
    tone_engine_render(&engine, g_stereo_a, 4000u);
    int silent_peak = peak_abs(g_stereo_a, 2000u, 4000u);
    CHECK(silent_peak == 0);

    tone_engine_set_amplitude(&engine, 0.25f);
    tone_engine_render(&engine, g_stereo_a, 4000u);
    peak = peak_abs(g_stereo_a, 2000u, 4000u);
    CHECK_NEAR((double)peak / 32767.0, 0.25, 0.01);
}

static void test_block_continuity(void)
{
    const tone_wave_t waveforms[] = {
        TONE_WAVE_SINE,
        TONE_WAVE_SQUARE,
        TONE_WAVE_TRIANGLE,
        TONE_WAVE_SAW,
    };
    const size_t chunk_sizes[] = {1u, 3u, 17u, 64u, 333u, 160u};
    const size_t frames = 5000u;

    for (size_t w = 0; w < sizeof(waveforms) / sizeof(waveforms[0]); w++) {
        tone_engine_t one_shot;
        tone_engine_t blocked;
        tone_engine_config_t cfg = {0};
        cfg.sample_rate = 16000u;
        cfg.waveform = waveforms[w];
        cfg.frequency_hz = 440.0f;
        cfg.amplitude = 0.7f;
        cfg.ramp_ms = 5.0f;
        CHECK(tone_engine_init(&one_shot, &cfg));
        CHECK(tone_engine_init(&blocked, &cfg));

        tone_engine_render(&one_shot, g_stereo_a, frames);

        size_t done = 0;
        size_t chunk = 0;
        while (done < frames) {
            size_t count = chunk_sizes[chunk % (sizeof(chunk_sizes) /
                                                sizeof(chunk_sizes[0]))];
            if (done + count > frames) {
                count = frames - done;
            }
            tone_engine_render(&blocked,
                               g_stereo_b + done * 2u, count);
            done += count;
            chunk++;
        }

        CHECK(memcmp(g_stereo_a, g_stereo_b,
                     frames * 2u * sizeof(int16_t)) == 0);
    }
}

static void test_click_free_parameter_change(void)
{
    tone_engine_t engine;
    tone_engine_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.waveform = TONE_WAVE_SINE;
    cfg.frequency_hz = 220.0f;
    cfg.amplitude = 0.5f;
    cfg.ramp_ms = 5.0f;
    CHECK(tone_engine_init(&engine, &cfg));

    const size_t before = 4800u;
    const size_t after = 4800u;
    tone_engine_render(&engine, g_stereo_a, before);

    tone_engine_set_frequency(&engine, 440.0f);
    tone_engine_set_amplitude(&engine, 0.25f);
    tone_engine_render(&engine, g_stereo_a + before * 2u, after);

    int maximum_delta = 0;
    for (size_t i = 1; i < before + after; i++) {
        int previous = g_stereo_a[(i - 1u) * 2u];
        int current = g_stereo_a[i * 2u];
        int delta = current - previous;
        if (delta < 0) {
            delta = -delta;
        }
        if (delta > maximum_delta) {
            maximum_delta = delta;
        }
    }
    CHECK(maximum_delta < 6000);

    int boundary_delta =
        g_stereo_a[before * 2u] - g_stereo_a[(before - 1u) * 2u];
    if (boundary_delta < 0) {
        boundary_delta = -boundary_delta;
    }
    CHECK(boundary_delta < 3000);

    double estimated = estimate_frequency(g_stereo_a, before + after, 16000.0,
                                          before + 1600u, 2400u);
    CHECK_NEAR(estimated, 440.0, 440.0 * 0.005);

    int peak = peak_abs(g_stereo_a, before + 1600u, before + after);
    CHECK_NEAR((double)peak / 32767.0, 0.25, 0.02);
}

static void check_sweep(tone_sweep_mode_t mode, double start_hz,
                        double end_hz)
{
    tone_engine_t engine;
    tone_engine_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.waveform = TONE_WAVE_SINE;
    cfg.frequency_hz = (float)start_hz;
    cfg.amplitude = 0.8f;
    cfg.sweep_mode = mode;
    cfg.sweep_start_hz = (float)start_hz;
    cfg.sweep_end_hz = (float)end_hz;
    cfg.sweep_duration_s = 0.8f;
    cfg.ramp_ms = 5.0f;
    CHECK(tone_engine_init(&engine, &cfg));

    size_t frames = 16000u;
    tone_engine_render(&engine, g_stereo_a, frames);

    double early =
        estimate_frequency(g_stereo_a, frames, 16000.0, 1600u, 1600u);
    double late =
        estimate_frequency(g_stereo_a, frames, 16000.0, 11200u, 1600u);
    CHECK(early > start_hz * 0.9);
    CHECK(early < end_hz * 0.75);
    CHECK(late > end_hz * 0.75);
    CHECK(late < end_hz * 1.1);
    CHECK(late > early * 1.5);
}

static void test_waveform_switch_click_free(void)
{
    tone_engine_t engine;
    tone_engine_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.waveform = TONE_WAVE_SINE;
    cfg.frequency_hz = 220.0f;
    cfg.amplitude = 0.5f;
    cfg.ramp_ms = 5.0f;
    CHECK(tone_engine_init(&engine, &cfg));

    const size_t half = 4800u;
    tone_engine_render(&engine, g_stereo_a, half);
    CHECK(tone_engine_set_waveform(&engine, TONE_WAVE_TRIANGLE));
    tone_engine_render(&engine, g_stereo_a + half * 2u, half);

    int maximum_delta = 0;
    for (size_t i = 1; i < half * 2u; i++) {
        int delta = g_stereo_a[i * 2u] - g_stereo_a[(i - 1u) * 2u];
        if (delta < 0) {
            delta = -delta;
        }
        if (delta > maximum_delta) {
            maximum_delta = delta;
        }
    }
    CHECK(maximum_delta < 5000);
}

static void test_sweeps(void)
{
    check_sweep(TONE_SWEEP_LINEAR, 200.0, 800.0);
    check_sweep(TONE_SWEEP_LOG, 100.0, 1000.0);
}

static void test_mono_matches_stereo(void)
{
    tone_engine_t stereo_engine;
    tone_engine_t mono_engine;
    tone_engine_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    cfg.waveform = TONE_WAVE_SINE;
    cfg.frequency_hz = 440.0f;
    cfg.amplitude = 0.6f;
    cfg.ramp_ms = 5.0f;
    CHECK(tone_engine_init(&stereo_engine, &cfg));
    CHECK(tone_engine_init(&mono_engine, &cfg));

    const size_t frames = 2000u;
    tone_engine_render(&stereo_engine, g_stereo_a, frames);
    tone_engine_render_mono(&mono_engine, g_stereo_b, frames);
    for (size_t i = 0; i < frames; i++) {
        CHECK(g_stereo_a[i * 2u] == g_stereo_b[i]);
        CHECK(g_stereo_a[i * 2u + 1u] == g_stereo_b[i]);
    }
}

static void test_nulls(void)
{
    tone_engine_t engine;
    CHECK(!tone_engine_init(NULL, NULL));
    CHECK(!tone_engine_init(&engine, NULL));
    tone_engine_render(NULL, NULL, 0u);
    tone_engine_render_mono(NULL, NULL, 0u);

    tone_engine_config_t cfg = {0};
    cfg.sample_rate = 16000u;
    CHECK(tone_engine_init(&engine, &cfg));
    tone_engine_render(&engine, NULL, 0u);
    tone_engine_render_mono(&engine, NULL, 0u);
}

int main(void)
{
    test_all_waveforms();
    test_amplitude();
    test_block_continuity();
    test_click_free_parameter_change();
    test_waveform_switch_click_free();
    test_sweeps();
    test_mono_matches_stereo();
    test_nulls();
    return test_report("test_tone");
}
