#include "tuner_engine.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define TEST_TWO_PI 6.283185307179586476925286766559

typedef enum {
    GEN_SINE,
    GEN_SAW,
    GEN_SQUARE,
    GEN_TRIANGLE,
    GEN_NOISE,
} gen_shape_t;

typedef struct {
    double frequency_hz;
    double amplitude;
    double phase;
    uint32_t rng;
    gen_shape_t shape;
} gen_t;

static void *test_alloc(size_t size)
{
    return malloc(size);
}

static void test_free(void *ptr)
{
    free(ptr);
}

static void gen_fill(gen_t *gen, uint32_t sample_rate, int16_t *out,
                     size_t frames)
{
    double increment =
        TEST_TWO_PI * gen->frequency_hz / (double)sample_rate;
    for (size_t i = 0; i < frames; i++) {
        double value;
        switch (gen->shape) {
        case GEN_SAW:
            value = 2.0 * (gen->phase / TEST_TWO_PI) - 1.0;
            break;
        case GEN_SQUARE:
            value = (gen->phase < TEST_TWO_PI * 0.5) ? 1.0 : -1.0;
            break;
        case GEN_TRIANGLE:
            value = 1.0 - 4.0 * fabs(gen->phase / TEST_TWO_PI - 0.5);
            break;
        case GEN_NOISE: {
            gen->rng = gen->rng * 1664525u + 1013904223u;
            value = ((double)(gen->rng >> 8u) / 8388608.0) - 1.0;
            break;
        }
        case GEN_SINE:
        default:
            value = sin(gen->phase);
            break;
        }
        double scaled = value * gen->amplitude * 32767.0;
        long sample = lrint(scaled);
        if (sample > 32767) {
            sample = 32767;
        } else if (sample < -32768) {
            sample = -32768;
        }
        out[i] = (int16_t)sample;

        gen->phase += increment;
        if (gen->phase >= TEST_TWO_PI) {
            gen->phase -= TEST_TWO_PI;
        }
    }
}

static tuner_engine_t *make_tuner(uint32_t sample_rate, uint32_t window,
                                  float a4_hz)
{
    tuner_engine_config_t cfg = {0};
    cfg.sample_rate = sample_rate;
    cfg.window_size = window;
    cfg.a4_hz = a4_hz;
    cfg.alloc = test_alloc;
    cfg.free = test_free;
    return tuner_engine_create(&cfg);
}

static bool feed(tuner_engine_t *engine, gen_t *gen, uint32_t sample_rate,
                 size_t frames, size_t block, tuner_result_t *result)
{
    int16_t buffer[512];
    size_t done = 0;
    bool analyzed = false;

    while (done < frames) {
        size_t count = frames - done;
        if (count > block) {
            count = block;
        }
        if (count > sizeof(buffer) / sizeof(buffer[0])) {
            count = sizeof(buffer) / sizeof(buffer[0]);
        }
        gen_fill(gen, sample_rate, buffer, count);
        analyzed |= tuner_engine_process_block(engine, buffer, count);
        done += count;
    }
    return tuner_engine_get_result(engine, result) && analyzed;
}

static void check_note(double frequency, int midi, const char *name,
                       int octave, double tolerance_cents, uint32_t window)
{
    tuner_engine_t *engine = make_tuner(16000u, window, 440.0f);
    CHECK(engine != NULL);
    if (engine == NULL) {
        return;
    }

    gen_t gen = {
        .frequency_hz = frequency,
        .amplitude = 0.7,
        .phase = 0.137,
        .rng = 1u,
        .shape = GEN_SINE,
    };
    tuner_result_t result = {0};
    bool got = feed(engine, &gen, 16000u, 12000u, 160u, &result);

    CHECK(got);
    CHECK(result.valid);
    CHECK(result.midi_note == midi);
    CHECK(strcmp(result.note_name, name) == 0);
    CHECK(result.octave == octave);
    CHECK(result.confidence > 0.90f);
    double error_cents =
        1200.0 * log2((double)result.frequency_hz / frequency);
    CHECK_NEAR(error_cents, 0.0, tolerance_cents);

    tuner_engine_destroy(engine);
}

static void test_standard_tuning(void)
{
    /* Equal temperament from A4=440; low E is MIDI 40. */
    check_note(82.4069, 40, "E", 2, 3.0, 2048u);
    check_note(110.0000, 45, "A", 2, 3.0, 2048u);
    check_note(146.8324, 50, "D", 3, 3.0, 2048u);
    check_note(195.9977, 55, "G", 3, 3.0, 2048u);
    check_note(246.9417, 59, "B", 3, 3.0, 2048u);
    check_note(329.6276, 64, "E", 4, 3.0, 2048u);
    check_note(440.0000, 69, "A", 4, 1.0, 2048u);
}

static void test_48k_low_and_a4(void)
{
    tuner_engine_t *engine = make_tuner(48000u, 4096u, 440.0f);
    CHECK(engine != NULL);
    if (engine == NULL) {
        return;
    }

    gen_t low = {
        .frequency_hz = 82.4069,
        .amplitude = 0.7,
        .phase = 0.31,
        .rng = 1u,
        .shape = GEN_SINE,
    };
    tuner_result_t result = {0};
    CHECK(feed(engine, &low, 48000u, 30000u, 480u, &result));
    CHECK(result.valid);
    CHECK(result.midi_note == 40);
    double low_error =
        1200.0 * log2((double)result.frequency_hz / 82.4069);
    CHECK_NEAR(low_error, 0.0, 3.0);

    tuner_engine_reset(engine);
    gen_t a4 = {
        .frequency_hz = 440.0,
        .amplitude = 0.7,
        .phase = 0.72,
        .rng = 1u,
        .shape = GEN_SINE,
    };
    CHECK(feed(engine, &a4, 48000u, 30000u, 480u, &result));
    CHECK(result.valid);
    CHECK(result.midi_note == 69);
    double a4_error = 1200.0 * log2((double)result.frequency_hz / 440.0);
    CHECK_NEAR(a4_error, 0.0, 1.0);

    tuner_engine_destroy(engine);
}

static void test_silence_and_noise_invalid(void)
{
    tuner_engine_t *engine = make_tuner(16000u, 2048u, 440.0f);
    CHECK(engine != NULL);
    if (engine == NULL) {
        return;
    }

    int16_t silence[160] = {0};
    for (int i = 0; i < 20; i++) {
        tuner_engine_process_block(engine, silence, 160u);
    }
    tuner_result_t result = {0};
    CHECK(tuner_engine_get_result(engine, &result));
    CHECK(!result.valid);
    CHECK(result.frequency_hz == 0.0f);

    gen_t noise = {
        .frequency_hz = 0.0,
        .amplitude = 0.25,
        .phase = 0.0,
        .rng = 0x12345678u,
        .shape = GEN_NOISE,
    };
    CHECK(feed(engine, &noise, 16000u, 16000u, 160u, &result));
    CHECK(!result.valid);

    tuner_engine_destroy(engine);
}

static void test_a4_reference(void)
{
    tuner_engine_t *engine = make_tuner(16000u, 2048u, 432.0f);
    CHECK(engine != NULL);
    if (engine == NULL) {
        return;
    }

    gen_t gen = {
        .frequency_hz = 440.0,
        .amplitude = 0.7,
        .phase = 0.4,
        .rng = 1u,
        .shape = GEN_SINE,
    };
    tuner_result_t result = {0};
    CHECK(feed(engine, &gen, 16000u, 8000u, 160u, &result));
    CHECK(result.valid);
    CHECK(result.midi_note == 69);
    CHECK(strcmp(result.note_name, "A") == 0);
    CHECK(result.octave == 4);
    double expected_cents = 1200.0 * log2(440.0 / 432.0);
    CHECK_NEAR(result.cents, expected_cents, 1.0);

    tuner_engine_reset(engine);
    gen.frequency_hz = 432.0;
    gen.phase = 0.9;
    CHECK(feed(engine, &gen, 16000u, 8000u, 160u, &result));
    CHECK(result.valid);
    CHECK_NEAR(result.cents, 0.0, 0.5);

    /* Runtime reference change is used by the next analysis. */
    tuner_engine_reset(engine);
    tuner_engine_set_a4_reference(engine, 440.0f);
    gen.frequency_hz = 440.0;
    gen.phase = 0.25;
    CHECK(feed(engine, &gen, 16000u, 8000u, 160u, &result));
    CHECK(result.valid);
    CHECK_NEAR(result.cents, 0.0, 0.5);

    tuner_engine_destroy(engine);
}

static void test_harmonic_waveforms(void)
{
    tuner_engine_t *engine = make_tuner(16000u, 2048u, 440.0f);
    CHECK(engine != NULL);
    if (engine == NULL) {
        return;
    }

    gen_t saw = {
        .frequency_hz = 110.0,
        .amplitude = 0.7,
        .phase = 0.21,
        .rng = 1u,
        .shape = GEN_SAW,
    };
    tuner_result_t result = {0};
    CHECK(feed(engine, &saw, 16000u, 12000u, 160u, &result));
    CHECK(result.valid);
    CHECK(result.midi_note == 45);
    CHECK(strcmp(result.note_name, "A") == 0);
    CHECK_NEAR(1200.0 * log2((double)result.frequency_hz / 110.0), 0.0, 5.0);

    tuner_engine_reset(engine);
    gen_t square = {
        .frequency_hz = 195.9977,
        .amplitude = 0.7,
        .phase = 0.63,
        .rng = 1u,
        .shape = GEN_SQUARE,
    };
    CHECK(feed(engine, &square, 16000u, 12000u, 160u, &result));
    CHECK(result.valid);
    CHECK(result.midi_note == 55);
    CHECK(strcmp(result.note_name, "G") == 0);
    CHECK_NEAR(1200.0 * log2((double)result.frequency_hz / 195.9977), 0.0,
               5.0);

    tuner_engine_destroy(engine);
}

static void test_arbitrary_block_sizes(void)
{
    tuner_engine_t *engine = make_tuner(16000u, 2048u, 440.0f);
    CHECK(engine != NULL);
    if (engine == NULL) {
        return;
    }

    gen_t gen = {
        .frequency_hz = 440.0,
        .amplitude = 0.7,
        .phase = 0.11,
        .rng = 1u,
        .shape = GEN_SINE,
    };
    int16_t buffer[512];
    size_t done = 0;
    const size_t blocks[] = {1u, 7u, 31u, 160u, 333u};
    size_t block_index = 0;
    bool analyzed = false;
    while (done < 9000u) {
        size_t count = blocks[block_index % (sizeof(blocks) /
                                            sizeof(blocks[0]))];
        if (done + count > 9000u) {
            count = 9000u - done;
        }
        gen_fill(&gen, 16000u, buffer, count);
        analyzed |= tuner_engine_process_block(engine, buffer, count);
        done += count;
        block_index++;
    }

    tuner_result_t result = {0};
    CHECK(analyzed);
    CHECK(tuner_engine_get_result(engine, &result));
    CHECK(result.valid);
    CHECK(result.midi_note == 69);
    CHECK_NEAR(1200.0 * log2((double)result.frequency_hz / 440.0), 0.0, 1.0);

    tuner_engine_destroy(engine);
}

static void test_bad_configuration_and_nulls(void)
{
    tuner_engine_config_t cfg = {0};
    cfg.alloc = test_alloc;
    cfg.free = test_free;
    cfg.min_frequency_hz = 1200.0f;
    cfg.max_frequency_hz = 40.0f;
    CHECK(tuner_engine_create(&cfg) == NULL);

    cfg = (tuner_engine_config_t){0};
    cfg.min_frequency_hz = 40.0f;
    cfg.max_frequency_hz = 1200.0f;
    CHECK(tuner_engine_create(&cfg) == NULL); /* no allocator */

    CHECK(tuner_engine_create(NULL) == NULL);
    CHECK(!tuner_engine_process_block(NULL, NULL, 0u));
    CHECK(!tuner_engine_get_result(NULL, NULL));
}

int main(void)
{
    test_standard_tuning();
    test_48k_low_and_a4();
    test_silence_and_noise_invalid();
    test_a4_reference();
    test_harmonic_waveforms();
    test_arbitrary_block_sizes();
    test_bad_configuration_and_nulls();
    return test_report("test_tuner");
}
