#include "organism_engine.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define TEST_WIDTH 256
#define TEST_HEIGHT 150
#define TEST_PARTICLES 512
#define TEST_FRAMES 300
#define TEST_PIXELS ((size_t)TEST_WIDTH * (size_t)TEST_HEIGHT)

typedef struct {
    organism_engine_t *engine;
    uint16_t *buffers[ORGANISM_BUFFER_COUNT];
} fixture_t;

static void *host_alloc(size_t size)
{
    return malloc(size);
}

static void host_free(void *ptr)
{
    free(ptr);
}

static void fixture_free(fixture_t *fixture)
{
    organism_engine_destroy(fixture->engine);
    fixture->engine = NULL;
    for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
        free(fixture->buffers[i]);
        fixture->buffers[i] = NULL;
    }
}

static bool fixture_init(fixture_t *fixture, uint32_t seed)
{
    memset(fixture, 0, sizeof(*fixture));

    for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
        fixture->buffers[i] = malloc(TEST_PIXELS * sizeof(uint16_t));
        if (fixture->buffers[i] == NULL) {
            fixture_free(fixture);
            return false;
        }
    }

    organism_engine_config_t cfg = {
        .width = TEST_WIDTH,
        .height = TEST_HEIGHT,
        .particle_count = TEST_PARTICLES,
        .buffers = { fixture->buffers[0], fixture->buffers[1],
                     fixture->buffers[2] },
        .alloc = host_alloc,
        .free = host_free,
        .seed = seed,
    };
    fixture->engine = organism_engine_create(&cfg);
    if (fixture->engine == NULL) {
        fixture_free(fixture);
        return false;
    }
    return true;
}

static uint64_t hash_pixels(const uint16_t *pixels, size_t count)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < count; i++) {
        hash ^= (uint64_t)pixels[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static size_t count_nonzero(const uint16_t *pixels, size_t count)
{
    size_t nonzero = 0;
    for (size_t i = 0; i < count; i++) {
        if (pixels[i] != 0) {
            nonzero++;
        }
    }
    return nonzero;
}

static organism_audio_t audio_for_frame(int frame)
{
    float t = (float)frame;
    organism_audio_t audio;
    audio.bass = 0.5f + 0.5f * sinf(t * 0.13f);
    audio.mid = 0.5f + 0.5f * sinf(t * 0.071f + 1.3f);
    audio.treble = 0.5f + 0.5f * sinf(t * 0.17f + 2.1f);
    audio.level = 0.45f + 0.55f * (0.5f + 0.5f * sinf(t * 0.043f));
    return audio;
}

static organism_touch_t touch_for_frame(int frame)
{
    organism_touch_t touch;
    touch.active = (frame % 37) != 0;
    int x = (frame * 5) % (TEST_WIDTH + 60) - 30;
    int y = TEST_HEIGHT / 2 +
            (int)(0.35f * (float)TEST_HEIGHT * sinf((float)frame * 0.09f));
    touch.x = (int16_t)x;
    touch.y = (int16_t)y;
    return touch;
}

static void test_create_validation(void)
{
    uint16_t *buffers[ORGANISM_BUFFER_COUNT] = { NULL, NULL, NULL };

    for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
        buffers[i] = malloc(TEST_PIXELS * sizeof(uint16_t));
        CHECK(buffers[i] != NULL);
    }
    if (buffers[0] == NULL || buffers[1] == NULL || buffers[2] == NULL) {
        for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
            free(buffers[i]);
        }
        return;
    }

    organism_engine_config_t cfg = {
        .width = TEST_WIDTH,
        .height = TEST_HEIGHT,
        .particle_count = TEST_PARTICLES,
        .buffers = { buffers[0], buffers[1], buffers[2] },
        .alloc = host_alloc,
        .free = host_free,
        .seed = 1u,
    };

    CHECK(organism_engine_create(NULL) == NULL);
    CHECK(organism_engine_step(NULL) == -1);
    organism_engine_set_audio(NULL, NULL);
    organism_engine_set_touch(NULL, NULL);
    cfg.particle_count = 0;
    CHECK(organism_engine_create(&cfg) == NULL);
    cfg.particle_count = 4097;
    CHECK(organism_engine_create(&cfg) == NULL);
    cfg.particle_count = TEST_PARTICLES;
    cfg.width = 0;
    CHECK(organism_engine_create(&cfg) == NULL);
    cfg.width = TEST_WIDTH;
    cfg.height = -1;
    CHECK(organism_engine_create(&cfg) == NULL);
    cfg.height = TEST_HEIGHT;
    cfg.alloc = NULL;
    CHECK(organism_engine_create(&cfg) == NULL);
    cfg.alloc = host_alloc;
    cfg.free = NULL;
    CHECK(organism_engine_create(&cfg) == NULL);
    cfg.free = host_free;
    cfg.buffers[1] = NULL;
    CHECK(organism_engine_create(&cfg) == NULL);
    cfg.buffers[1] = buffers[1];

    organism_engine_t *engine = organism_engine_create(&cfg);
    CHECK(engine != NULL);
    if (engine != NULL) {
        for (int i = 0; i < (int)ORGANISM_BUFFER_COUNT; i++) {
            CHECK(organism_engine_buffer(engine, i) == buffers[i]);
            CHECK(count_nonzero(buffers[i], TEST_PIXELS) == 0);
        }
        CHECK(organism_engine_buffer(engine, -1) == NULL);
        CHECK(organism_engine_buffer(engine, 3) == NULL);
        organism_engine_destroy(engine);
    }

    organism_engine_destroy(NULL);
    for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
        free(buffers[i]);
    }
}

static void test_determinism_and_rotation(void)
{
    fixture_t a;
    fixture_t b;
    fixture_t c;

    CHECK(fixture_init(&a, 0x12345678u));
    CHECK(fixture_init(&b, 0x12345678u));
    bool have_c = fixture_init(&c, 0x87654321u);
    CHECK(have_c);
    if (a.engine == NULL || b.engine == NULL) {
        fixture_free(&a);
        fixture_free(&b);
        fixture_free(&c);
        return;
    }

    uint64_t sum_a = 1469598103934665603ull;
    uint64_t sum_b = 1469598103934665603ull;
    uint64_t sum_c = 1469598103934665603ull;
    uint64_t first_hash[ORGANISM_BUFFER_COUNT] = { 0 };
    uint64_t last_hash[ORGANISM_BUFFER_COUNT] = { 0 };
    bool first_seen[ORGANISM_BUFFER_COUNT] = { false };
    int previous = -1;
    size_t last_nonzero = 0;

    for (int frame = 0; frame < TEST_FRAMES; frame++) {
        organism_audio_t audio = audio_for_frame(frame);
        organism_touch_t touch = touch_for_frame(frame);

        if (frame == 100) {
            audio.bass = NAN;
        } else if (frame == 101) {
            audio.mid = INFINITY;
        } else if (frame == 102) {
            audio.treble = -INFINITY;
            audio.level = 2.0f;
        } else if (frame == 103) {
            touch.active = true;
            touch.x = INT16_MAX;
            touch.y = INT16_MIN;
        }

        organism_engine_set_audio(a.engine, &audio);
        organism_engine_set_audio(b.engine, &audio);
        organism_engine_set_touch(a.engine, &touch);
        organism_engine_set_touch(b.engine, &touch);
        if (have_c) {
            organism_engine_set_audio(c.engine, &audio);
            organism_engine_set_touch(c.engine, &touch);
        }

        int index_a = organism_engine_step(a.engine);
        int index_b = organism_engine_step(b.engine);
        int index_c = have_c ? organism_engine_step(c.engine) : -1;

        CHECK(index_a >= 0 && index_a < (int)ORGANISM_BUFFER_COUNT);
        CHECK(index_a == index_b);
        if (previous >= 0) {
            CHECK(index_a == (previous + 1) % (int)ORGANISM_BUFFER_COUNT);
        }
        previous = index_a;

        const uint16_t *pixels_a = organism_engine_buffer(a.engine, index_a);
        const uint16_t *pixels_b = organism_engine_buffer(b.engine, index_b);
        CHECK(pixels_a == a.buffers[index_a]);
        CHECK(pixels_b == b.buffers[index_b]);

        uint64_t hash_a = hash_pixels(pixels_a, TEST_PIXELS);
        uint64_t hash_b = hash_pixels(pixels_b, TEST_PIXELS);
        CHECK(hash_a == hash_b);

        sum_a = (sum_a ^ hash_a) * 1099511628211ull;
        sum_b = (sum_b ^ hash_b) * 1099511628211ull;

        if (have_c) {
            const uint16_t *pixels_c = organism_engine_buffer(c.engine, index_c);
            uint64_t hash_c = hash_pixels(pixels_c, TEST_PIXELS);
            sum_c = (sum_c ^ hash_c) * 1099511628211ull;
        }

        if (!first_seen[index_a]) {
            first_seen[index_a] = true;
            first_hash[index_a] = hash_a;
        }
        last_hash[index_a] = hash_a;
        last_nonzero = count_nonzero(pixels_a, TEST_PIXELS);

        if (frame > 10) {
            CHECK(last_nonzero > 0);
        }
    }

    CHECK(sum_a == sum_b);
    if (have_c) {
        CHECK(sum_c != sum_a);
    }
    CHECK(last_nonzero > 0);

    for (int i = 0; i < (int)ORGANISM_BUFFER_COUNT; i++) {
        CHECK(first_seen[i]);
        CHECK(first_hash[i] != last_hash[i]);
        CHECK(count_nonzero(a.buffers[i], TEST_PIXELS) > 0);
    }

    fixture_free(&a);
    fixture_free(&b);
    fixture_free(&c);
}

static void test_invalid_inputs(void)
{
    fixture_t fixture;
    CHECK(fixture_init(&fixture, 7u));
    if (fixture.engine == NULL) {
        fixture_free(&fixture);
        return;
    }

    organism_audio_t audio = { NAN, INFINITY, -INFINITY, -5.0f };
    organism_touch_t touch = { 32000, -32000, true };

    for (int frame = 0; frame < 8; frame++) {
        organism_engine_set_audio(fixture.engine, &audio);
        organism_engine_set_touch(fixture.engine, &touch);
        int index = organism_engine_step(fixture.engine);
        CHECK(index >= 0 && index < (int)ORGANISM_BUFFER_COUNT);
        const uint16_t *pixels = organism_engine_buffer(fixture.engine, index);
        CHECK(pixels != NULL);
        CHECK(count_nonzero(pixels, TEST_PIXELS) > 0);
    }

    organism_engine_set_audio(fixture.engine, NULL);
    organism_engine_set_touch(fixture.engine, NULL);
    CHECK(organism_engine_step(fixture.engine) >= 0);

    fixture_free(&fixture);
}

int main(void)
{
    test_create_validation();
    test_determinism_and_rotation();
    test_invalid_inputs();
    return test_report("test_organism");
}
