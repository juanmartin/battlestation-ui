#include "audio_mixer.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "test_support.h"

#define TEST_FRAMES 160u
#define TEST_SAMPLES (TEST_FRAMES * 2u)

static int16_t lane_a[TEST_SAMPLES];
static int16_t lane_b[TEST_SAMPLES];
static int16_t lane_c[TEST_SAMPLES];
static int16_t out[TEST_SAMPLES];

static void fill_constant(int16_t *pcm, int16_t value)
{
    for (size_t i = 0; i < TEST_SAMPLES; i++) {
        pcm[i] = value;
    }
}

static void fill_alternating(int16_t *pcm, int16_t pos, int16_t neg)
{
    for (size_t i = 0; i < TEST_FRAMES; i++) {
        pcm[i * 2u + 0u] = (i & 1u) ? neg : pos;
        pcm[i * 2u + 1u] = (i & 1u) ? neg : pos;
    }
}

static void test_mono_duplication(void)
{
    int16_t mono[4] = { 100, -200, 300, -400 };
    int16_t stereo[8] = { 0 };

    audio_mixer_expand_mono(mono, 4, stereo);
    CHECK(stereo[0] == 100 && stereo[1] == 100);
    CHECK(stereo[2] == -200 && stereo[3] == -200);
    CHECK(stereo[4] == 300 && stereo[5] == 300);
    CHECK(stereo[6] == -400 && stereo[7] == -400);
}

static void test_silence(void)
{
    audio_mixer_lane_t lanes[AUDIO_MIXER_MAX_LANES] = { 0 };
    memset(out, 0x5A, sizeof(out));
    audio_mixer_process(lanes, AUDIO_MIXER_MAX_LANES, out, TEST_FRAMES);
    for (size_t i = 0; i < TEST_SAMPLES; i++) {
        CHECK(out[i] == 0);
    }

    memset(lane_a, 0, sizeof(lane_a));
    lanes[0].pcm = lane_a;
    lanes[0].frames = TEST_FRAMES;
    lanes[0].gain = 1.0f;
    lanes[0].active = true;
    memset(out, 0x5A, sizeof(out));
    audio_mixer_process(lanes, 1, out, TEST_FRAMES);
    for (size_t i = 0; i < TEST_SAMPLES; i++) {
        CHECK(out[i] == 0);
    }
}

static void test_gain(void)
{
    audio_mixer_lane_t lane = {
        .pcm = lane_a,
        .frames = TEST_FRAMES,
        .gain = 0.5f,
        .active = true,
    };

    fill_alternating(lane_a, 1000, -1000);
    audio_mixer_process(&lane, 1, out, TEST_FRAMES);
    CHECK(out[0] == 500);
    CHECK(out[1] == 500);
    CHECK(out[2] == -500);
    CHECK(out[3] == -500);
}

static void test_saturation(void)
{
    audio_mixer_lane_t lanes[3] = {
        { .pcm = lane_a, .frames = TEST_FRAMES, .gain = 1.0f, .active = true },
        { .pcm = lane_b, .frames = TEST_FRAMES, .gain = 1.0f, .active = true },
        { .pcm = lane_c, .frames = TEST_FRAMES, .gain = 1.0f, .active = true },
    };

    fill_constant(lane_a, 32767);
    fill_constant(lane_b, 32767);
    fill_constant(lane_c, 32767);
    audio_mixer_process(lanes, 3, out, TEST_FRAMES);
    for (size_t i = 0; i < TEST_SAMPLES; i++) {
        CHECK(out[i] == 32767);
    }

    fill_constant(lane_a, -32768);
    fill_constant(lane_b, -32768);
    fill_constant(lane_c, -32768);
    audio_mixer_process(lanes, 3, out, TEST_FRAMES);
    for (size_t i = 0; i < TEST_SAMPLES; i++) {
        CHECK(out[i] == -32768);
    }
}

static void test_ducking_gain_math(void)
{
    audio_mixer_lane_t lanes[3] = {
        { .pcm = lane_a, .frames = TEST_FRAMES, .gain = 1.0f, .active = true },
        { .pcm = lane_b, .frames = TEST_FRAMES,
          .gain = 0.35481339f, .active = true },
        { .pcm = lane_c, .frames = TEST_FRAMES,
          .gain = 0.17782794f, .active = true },
    };

    fill_constant(lane_a, 1000);
    fill_constant(lane_b, 1000);
    fill_constant(lane_c, 1000);
    audio_mixer_process(lanes, 3, out, TEST_FRAMES);
    CHECK_NEAR(out[0], 1531.0, 3.0);
    CHECK_NEAR(out[1], 1531.0, 3.0);
}

int main(void)
{
    test_mono_duplication();
    test_silence();
    test_gain();
    test_saturation();
    test_ducking_gain_math();
    return test_report("test_mixer");
}
