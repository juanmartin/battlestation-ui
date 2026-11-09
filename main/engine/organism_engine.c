#include "organism_engine.h"

#include <math.h>
#include <string.h>

#define ORGANISM_MAX_PARTICLES 4096
#define ORGANISM_MAX_DIMENSION 8192
#define ORGANISM_FIELD_SIZE 96
#define ORGANISM_FIELD_CELLS (ORGANISM_FIELD_SIZE * ORGANISM_FIELD_SIZE)
#define ORGANISM_LATTICE_SIZE 8
#define ORGANISM_PALETTE_HUES 64
#define ORGANISM_PALETTE_ROWS 8
#define ORGANISM_TWO_PI 6.283185307179586476925286766559

/*
 * One field cell. The vector is normalized to roughly 127 units so it can be
 * rotated and scaled with small integer math in the per-particle loop.
 */
typedef struct {
    int8_t x;
    int8_t y;
} organism_field_vec_t;

/*
 * Positions and velocities use 1/16 pixel units (Q12.4). A 1024x600 frame
 * needs 18 bits of position, so the position is int32_t while the velocity
 * fits comfortably in int16_t. The struct is 16 bytes on both host and target.
 */
typedef struct {
    int32_t x;
    int32_t y;
    int16_t vx;
    int16_t vy;
    uint8_t hue; /* 0..63 palette phase */
    uint8_t vis; /* 0..255 visibility threshold driven by mid */
} organism_particle_t;

_Static_assert(sizeof(organism_particle_t) == 16,
               "organism particle must stay compact");

struct organism_engine {
    organism_free_fn free_fn;
    int width;
    int height;
    size_t pixel_count;
    uint16_t *buffers[ORGANISM_BUFFER_COUNT];
    int front;
    int particle_count;
    organism_particle_t *particles;
    organism_field_vec_t *field;
    uint32_t rng;
    bool rotate_180;
    organism_audio_t audio;
    organism_audio_t smooth;
    organism_touch_t touch;
    int32_t touch_radius_sq;
    uint32_t touch_inv_radius_q24;
    uint32_t field_scale_x;
    uint32_t field_scale_y;
    int16_t rot_cos[64];
    int16_t rot_sin[64];
    uint8_t rot_phase;
    uint8_t rot_tick;
    uint16_t palette[ORGANISM_PALETTE_ROWS * ORGANISM_PALETTE_HUES];
};

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    *state = x;
    return x;
}

static float rng_unit(uint32_t *state)
{
    return (float)(rng_next(state) >> 8u) * (1.0f / 16777216.0f);
}

static float sanitize_unit(float value)
{
    if (!isfinite(value)) {
        return 0.0f;
    }
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= 1.0f) {
        return 1.0f;
    }
    return value;
}

static float smooth_toward(float current, float target, float factor)
{
    return sanitize_unit(current + (target - current) * factor);
}

static float field_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static void build_field(organism_field_vec_t *field, uint32_t *rng)
{
    float lx[ORGANISM_LATTICE_SIZE][ORGANISM_LATTICE_SIZE];
    float ly[ORGANISM_LATTICE_SIZE][ORGANISM_LATTICE_SIZE];

    for (int y = 0; y < ORGANISM_LATTICE_SIZE; y++) {
        for (int x = 0; x < ORGANISM_LATTICE_SIZE; x++) {
            float angle = rng_unit(rng) * (float)ORGANISM_TWO_PI;
            lx[y][x] = cosf(angle);
            ly[y][x] = sinf(angle);
        }
    }

    const float lattice = (float)ORGANISM_LATTICE_SIZE;
    const float size = (float)ORGANISM_FIELD_SIZE;

    for (int y = 0; y < ORGANISM_FIELD_SIZE; y++) {
        float gy = ((float)y + 0.5f) * lattice / size;
        int y0 = (int)gy;
        if (y0 >= ORGANISM_LATTICE_SIZE) {
            y0 = ORGANISM_LATTICE_SIZE - 1;
        }
        int y1 = (y0 + 1) & (ORGANISM_LATTICE_SIZE - 1);
        float fy = gy - (float)y0;
        fy = fy * fy * (3.0f - 2.0f * fy);
        float ny = ((float)y + 0.5f) * (2.0f / size) - 1.0f;

        for (int x = 0; x < ORGANISM_FIELD_SIZE; x++) {
            float gx = ((float)x + 0.5f) * lattice / size;
            int x0 = (int)gx;
            if (x0 >= ORGANISM_LATTICE_SIZE) {
                x0 = ORGANISM_LATTICE_SIZE - 1;
            }
            int x1 = (x0 + 1) & (ORGANISM_LATTICE_SIZE - 1);
            float fx = gx - (float)x0;
            fx = fx * fx * (3.0f - 2.0f * fx);

            float vx = field_lerp(field_lerp(lx[y0][x0], lx[y0][x1], fx),
                                  field_lerp(lx[y1][x0], lx[y1][x1], fx), fy);
            float vy = field_lerp(field_lerp(ly[y0][x0], ly[y0][x1], fx),
                                  field_lerp(ly[y1][x0], ly[y1][x1], fx), fy);

            /* A gentle swirl and radial breath keep the flow organism-like. */
            float nx = ((float)x + 0.5f) * (2.0f / size) - 1.0f;
            vx += -ny * 0.35f + nx * 0.10f;
            vy += nx * 0.35f + ny * 0.10f;

            float len = sqrtf(vx * vx + vy * vy);
            if (!(len > 1e-6f)) {
                vx = 1.0f;
                vy = 0.0f;
                len = 1.0f;
            }

            int ix = (int)(vx / len * 127.0f);
            int iy = (int)(vy / len * 127.0f);
            if (ix > 127) {
                ix = 127;
            } else if (ix < -127) {
                ix = -127;
            }
            if (iy > 127) {
                iy = 127;
            } else if (iy < -127) {
                iy = -127;
            }
            field[y * ORGANISM_FIELD_SIZE + x].x = (int8_t)ix;
            field[y * ORGANISM_FIELD_SIZE + x].y = (int8_t)iy;
        }
    }
}

static uint8_t lerp_u8(int a, int b, int step, int steps)
{
    return (uint8_t)(a + (b - a) * step / steps);
}

static uint16_t rgb565_from_u8(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void build_palette(uint16_t *palette)
{
    uint8_t base[ORGANISM_PALETTE_HUES][3];

    /* Cyan -> magenta -> amber, then mirror back so hue wraps smoothly. */
    for (int i = 0; i < 16; i++) {
        base[i][0] = lerp_u8(0, 240, i, 15);
        base[i][1] = lerp_u8(235, 60, i, 15);
        base[i][2] = lerp_u8(255, 255, i, 15);
    }
    for (int i = 0; i < 16; i++) {
        base[16 + i][0] = lerp_u8(240, 255, i, 15);
        base[16 + i][1] = lerp_u8(60, 180, i, 15);
        base[16 + i][2] = lerp_u8(255, 10, i, 15);
    }
    for (int i = 0; i < 32; i++) {
        base[32 + i][0] = base[31 - i][0];
        base[32 + i][1] = base[31 - i][1];
        base[32 + i][2] = base[31 - i][2];
    }

    for (int row = 0; row < ORGANISM_PALETTE_ROWS; row++) {
        for (int h = 0; h < ORGANISM_PALETTE_HUES; h++) {
            int r = (base[h][0] * row + 3) / 7;
            int g = (base[h][1] * row + 3) / 7;
            int b = (base[h][2] * row + 3) / 7;
            palette[row * ORGANISM_PALETTE_HUES + h] = rgb565_from_u8(r, g, b);
        }
    }
}

static void build_rotation(organism_engine_t *engine)
{
    for (int i = 0; i < 64; i++) {
        float angle = (float)ORGANISM_TWO_PI * (float)i / 64.0f;
        engine->rot_cos[i] = (int16_t)lrintf(cosf(angle) * 127.0f);
        engine->rot_sin[i] = (int16_t)lrintf(sinf(angle) * 127.0f);
    }
}

static void init_particles(organism_engine_t *engine)
{
    uint32_t x_limit = (uint32_t)engine->width << 4u;
    uint32_t y_limit = (uint32_t)engine->height << 4u;

    for (int i = 0; i < engine->particle_count; i++) {
        organism_particle_t *p = &engine->particles[i];
        p->x = (int32_t)(rng_next(&engine->rng) % x_limit);
        p->y = (int32_t)(rng_next(&engine->rng) % y_limit);
        p->vx = 0;
        p->vy = 0;
        p->hue = (uint8_t)(rng_next(&engine->rng) & 63u);
        p->vis = (uint8_t)(rng_next(&engine->rng) >> 24u);
    }
}

static int32_t wrap_position(int32_t value, int32_t limit)
{
    if (value >= 0 && value < limit) {
        return value;
    }
    value %= limit;
    if (value < 0) {
        value += limit;
    }
    return value;
}

static inline void put_pixel(uint16_t *buffer, int width, int height,
                             int x, int y, uint16_t color)
{
    if ((unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height) {
        buffer[(size_t)y * (size_t)width + (size_t)x] = color;
    }
}

/* Particle coordinates are logical; the panel-facing buffer may be 180°
 * rotated. This keeps the expensive 180° transform out of the copy loop. */
static inline void plot_pixel(organism_engine_t *engine, uint16_t *buffer,
                              int x, int y, uint16_t color)
{
    if (engine->rotate_180) {
        x = engine->width - 1 - x;
        y = engine->height - 1 - y;
    }
    put_pixel(buffer, engine->width, engine->height, x, y, color);
}

/*
 * Copy the previous frame while applying a channel-safe 50% fade. The current
 * front buffer is never mutated: the display may still be scanning it, so
 * triple buffering requires copy + fade into the next buffer.
 */
static void fade_copy(const uint16_t *src, uint16_t *dst, size_t count)
{
    size_t i = 0;
    const bool aligned = (((uintptr_t)src & 3u) == 0u) && (((uintptr_t)dst & 3u) == 0u);
    if (aligned) {
        const uint32_t *s32 = (const uint32_t *)src;
        uint32_t *d32 = (uint32_t *)dst;
        const size_t words = count / 2u;
        for (size_t w = 0; w < words; w++) {
            d32[w] = (s32[w] >> 1u) & 0x7BEF7BEFu;
        }
        i = words * 2u;
    } else {
        for (; i + 1u < count; i += 2u) {
            uint32_t pair;
            memcpy(&pair, src + i, sizeof(pair));
            pair = (pair >> 1u) & 0x7BEF7BEFu;
            memcpy(dst + i, &pair, sizeof(pair));
        }
    }
    if (i < count) {
        dst[i] = (uint16_t)((src[i] >> 1u) & 0x7BEFu);
    }
}

organism_engine_t *organism_engine_create(const organism_engine_config_t *cfg)
{
    if (cfg == NULL || cfg->alloc == NULL || cfg->free == NULL) {
        return NULL;
    }
    if (cfg->width <= 0 || cfg->height <= 0 ||
        cfg->width > ORGANISM_MAX_DIMENSION ||
        cfg->height > ORGANISM_MAX_DIMENSION) {
        return NULL;
    }
    if (cfg->particle_count <= 0 || cfg->particle_count > ORGANISM_MAX_PARTICLES) {
        return NULL;
    }
    int buffer_count = 0;
    for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
        if (cfg->buffers[i] != NULL) {
            buffer_count++;
        }
    }
    if (buffer_count != 0 && buffer_count != (int)ORGANISM_BUFFER_COUNT) {
        return NULL;
    }

    organism_engine_t *engine = cfg->alloc(sizeof(*engine));
    if (engine == NULL) {
        return NULL;
    }
    memset(engine, 0, sizeof(*engine));

    engine->free_fn = cfg->free;
    engine->width = cfg->width;
    engine->height = cfg->height;
    engine->pixel_count = (size_t)cfg->width * (size_t)cfg->height;
    engine->particle_count = cfg->particle_count;
    for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
        engine->buffers[i] = cfg->buffers[i];
    }
    engine->front = 0;
    engine->rng = cfg->seed != 0 ? cfg->seed : 0x9E3779B9u;
    engine->rotate_180 = cfg->rotate_180;

    engine->particles = cfg->alloc((size_t)cfg->particle_count *
                                   sizeof(*engine->particles));
    if (engine->particles == NULL) {
        cfg->free(engine);
        return NULL;
    }
    engine->field = cfg->alloc((size_t)ORGANISM_FIELD_CELLS *
                               sizeof(*engine->field));
    if (engine->field == NULL) {
        cfg->free(engine->particles);
        cfg->free(engine);
        return NULL;
    }

    size_t bytes = engine->pixel_count * sizeof(uint16_t);
    if (engine->buffers[0] != NULL) {
        for (size_t i = 0; i < ORGANISM_BUFFER_COUNT; i++) {
            memset(engine->buffers[i], 0, bytes);
        }
    }

    engine->field_scale_x = ((uint32_t)ORGANISM_FIELD_SIZE << 16u) /
                            (uint32_t)engine->width;
    engine->field_scale_y = ((uint32_t)ORGANISM_FIELD_SIZE << 16u) /
                            (uint32_t)engine->height;

    int radius = (engine->width < engine->height ? engine->width : engine->height) / 4;
    if (radius < 24) {
        radius = 24;
    }
    engine->touch_radius_sq = (int32_t)radius * (int32_t)radius;
    engine->touch_inv_radius_q24 =
        (uint32_t)((1u << 24u) / (uint32_t)engine->touch_radius_sq);

    build_field(engine->field, &engine->rng);
    build_palette(engine->palette);
    build_rotation(engine);
    init_particles(engine);

    return engine;
}

void organism_engine_destroy(organism_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }
    organism_free_fn free_fn = engine->free_fn;
    free_fn(engine->particles);
    free_fn(engine->field);
    free_fn(engine);
}

void organism_engine_set_audio(organism_engine_t *engine, const organism_audio_t *audio)
{
    if (engine == NULL || audio == NULL) {
        return;
    }
    engine->audio.bass = sanitize_unit(audio->bass);
    engine->audio.mid = sanitize_unit(audio->mid);
    engine->audio.treble = sanitize_unit(audio->treble);
    engine->audio.level = sanitize_unit(audio->level);
}

void organism_engine_set_touch(organism_engine_t *engine, const organism_touch_t *touch)
{
    if (engine == NULL) {
        return;
    }
    if (touch == NULL || !touch->active) {
        engine->touch.active = false;
        engine->touch.x = 0;
        engine->touch.y = 0;
        return;
    }
    engine->touch.active = true;
    engine->touch.x = touch->x;
    engine->touch.y = touch->y;
}

static void render_frame(organism_engine_t *engine, uint16_t *dst, const uint16_t *src)
{
    if (engine == NULL || dst == NULL) {
        return;
    }

    engine->smooth.bass = smooth_toward(engine->smooth.bass, engine->audio.bass, 0.35f);
    engine->smooth.mid = smooth_toward(engine->smooth.mid, engine->audio.mid, 0.25f);
    engine->smooth.treble =
        smooth_toward(engine->smooth.treble, engine->audio.treble, 0.25f);
    engine->smooth.level = smooth_toward(engine->smooth.level, engine->audio.level, 0.25f);

    const float bass = engine->smooth.bass;
    const float mid = engine->smooth.mid;
    const float treble = engine->smooth.treble;
    const float level = engine->smooth.level;

    int32_t speed_q4 = 8 + (int32_t)(bass * 56.0f + 0.5f);
    int vis_limit = 128 + (int)(mid * 127.0f + 0.5f);
    if (vis_limit > 255) {
        vis_limit = 255;
    }
    int hue_shift = (int)(treble * 63.0f + 0.5f) & 63;
    int row = 1 + (int)(level * 6.0f + 0.5f);
    if (row < 1) {
        row = 1;
    } else if (row > ORGANISM_PALETTE_ROWS - 1) {
        row = ORGANISM_PALETTE_ROWS - 1;
    }
    bool repel = bass > 0.62f;

    int16_t rc = engine->rot_cos[engine->rot_phase];
    int16_t rs = engine->rot_sin[engine->rot_phase];
    int32_t kc = ((int32_t)rc * speed_q4) / 128;
    int32_t ks = ((int32_t)rs * speed_q4) / 128;
    if (++engine->rot_tick >= 5u) {
        engine->rot_tick = 0;
        engine->rot_phase = (uint8_t)((engine->rot_phase + 1u) & 63u);
    }

    bool touch_active = engine->touch.active;
    int touch_x = 0;
    int touch_y = 0;
    if (touch_active) {
        touch_x = engine->touch.x;
        touch_y = engine->touch.y;
        if (touch_x < 0) {
            touch_x = 0;
        } else if (touch_x >= engine->width) {
            touch_x = engine->width - 1;
        }
        if (touch_y < 0) {
            touch_y = 0;
        } else if (touch_y >= engine->height) {
            touch_y = engine->height - 1;
        }
    }

    if (src != NULL) {
        fade_copy(src, dst, engine->pixel_count);
    } else {
        memset(dst, 0, engine->pixel_count * sizeof(uint16_t));
    }

    const int32_t max_x = (int32_t)engine->width << 4;
    const int32_t max_y = (int32_t)engine->height << 4;

    for (int i = 0; i < engine->particle_count; i++) {
        organism_particle_t *p = &engine->particles[i];

        int32_t px = p->x >> 4;
        int32_t py = p->y >> 4;

        uint32_t fx = ((uint32_t)px * engine->field_scale_x) >> 16u;
        uint32_t fy = ((uint32_t)py * engine->field_scale_y) >> 16u;
        if (fx >= ORGANISM_FIELD_SIZE) {
            fx = ORGANISM_FIELD_SIZE - 1u;
        }
        if (fy >= ORGANISM_FIELD_SIZE) {
            fy = ORGANISM_FIELD_SIZE - 1u;
        }
        const organism_field_vec_t *fv =
            &engine->field[(size_t)fy * ORGANISM_FIELD_SIZE + fx];

        /* Rotate the sampled field vector and apply bass-driven speed. */
        int32_t tvx = ((int32_t)fv->x * kc - (int32_t)fv->y * ks) / 128;
        int32_t tvy = ((int32_t)fv->x * ks + (int32_t)fv->y * kc) / 128;
        int32_t push_x = 0;
        int32_t push_y = 0;

        if (touch_active) {
            int32_t dx = (int32_t)touch_x - px;
            int32_t dy = (int32_t)touch_y - py;
            int32_t dist_sq = dx * dx + dy * dy;
            if (dist_sq < engine->touch_radius_sq) {
                uint32_t falloff =
                    (uint32_t)(engine->touch_radius_sq - dist_sq);
                uint32_t falloff_q8 =
                    (falloff * engine->touch_inv_radius_q24) >> 16u;
                int32_t ax = (dx * (int32_t)falloff_q8) / 512;
                int32_t ay = (dy * (int32_t)falloff_q8) / 512;
                if (repel) {
                    push_x = -ax / 4;
                    push_y = -ay / 4;
                } else {
                    push_x = ax / 4;
                    push_y = ay / 4;
                }
            }
        }

        if (tvx > 512) {
            tvx = 512;
        } else if (tvx < -512) {
            tvx = -512;
        }
        if (tvy > 512) {
            tvy = 512;
        } else if (tvy < -512) {
            tvy = -512;
        }

        int32_t nvx = (int32_t)p->vx + (tvx - (int32_t)p->vx) / 4 + push_x;
        int32_t nvy = (int32_t)p->vy + (tvy - (int32_t)p->vy) / 4 + push_y;
        p->vx = (int16_t)nvx;
        p->vy = (int16_t)nvy;

        p->x = wrap_position(p->x + p->vx, max_x);
        p->y = wrap_position(p->y + p->vy, max_y);

        if (p->vis <= vis_limit) {
            int hue = (p->hue + hue_shift) & 63;
            uint16_t color = engine->palette[row * ORGANISM_PALETTE_HUES + hue];
            int x = (int)(p->x >> 4);
            int y = (int)(p->y >> 4);

            plot_pixel(engine, dst, x, y, color);
            plot_pixel(engine, dst, x + 1, y, color);
            plot_pixel(engine, dst, x, y + 1, color);
            plot_pixel(engine, dst, x + 1, y + 1, color);

            /* A faint cross halo makes the 2x2 cores read as soft dots. */
            int halo_row = row > 2 ? row - 2 : 1;
            uint16_t halo =
                engine->palette[halo_row * ORGANISM_PALETTE_HUES + hue];
            plot_pixel(engine, dst, x - 1, y, halo);
            plot_pixel(engine, dst, x + 2, y, halo);
            plot_pixel(engine, dst, x, y - 1, halo);
            plot_pixel(engine, dst, x, y + 2, halo);

            /* A dim tail pixel helps fast particles read as short segments. */
            if (p->vx > 16 || p->vx < -16 || p->vy > 16 || p->vy < -16) {
                int32_t tx = wrap_position(p->x - (int32_t)p->vx * 2, max_x);
                int32_t ty = wrap_position(p->y - (int32_t)p->vy * 2, max_y);
                int tail_row = row > 2 ? row - 2 : 1;
                uint16_t tail =
                    engine->palette[tail_row * ORGANISM_PALETTE_HUES + hue];
                plot_pixel(engine, dst, (int)(tx >> 4), (int)(ty >> 4), tail);
            }
        }
    }

}

int organism_engine_step(organism_engine_t *engine)
{
    if (engine == NULL || engine->buffers[0] == NULL) {
        return -1;
    }
    const int next = (int)((unsigned)(engine->front + 1) % ORGANISM_BUFFER_COUNT);
    render_frame(engine, engine->buffers[next], engine->buffers[engine->front]);
    engine->front = next;
    return next;
}

void organism_engine_step_into(organism_engine_t *engine, uint16_t *dst,
                               const uint16_t *src)
{
    render_frame(engine, dst, src);
}

const uint16_t *organism_engine_buffer(const organism_engine_t *engine, int index)
{
    if (engine == NULL || engine->buffers[0] == NULL ||
        index < 0 || index >= (int)ORGANISM_BUFFER_COUNT) {
        return NULL;
    }
    return engine->buffers[index];
}
