#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ORGANISM_BUFFER_COUNT 3u

typedef void *(*organism_alloc_fn)(size_t size);
typedef void (*organism_free_fn)(void *ptr);

typedef struct {
    int width;
    int height;
    int particle_count;                 /* <= 4096, > 0 */
    uint16_t *buffers[ORGANISM_BUFFER_COUNT]; /* caller-owned RGB565 */
    organism_alloc_fn alloc;
    organism_free_fn free;
    uint32_t seed;
    bool rotate_180; /* render the panel's physical orientation (180°) */
} organism_engine_config_t;

typedef struct {
    float bass;    /* 0..1 */
    float mid;     /* 0..1 */
    float treble;  /* 0..1 */
    float level;   /* 0..1 */
} organism_audio_t;

typedef struct {
    int16_t x;
    int16_t y;
    bool active;
} organism_touch_t;

typedef struct organism_engine organism_engine_t;

/*
 * Each cfg->buffers[i] must hold width * height RGB565 pixels. The engine
 * clears the three buffers on create and owns no framebuffers. All engine
 * state is allocated through cfg->alloc and released through cfg->free.
 *
 * The engine is not internally synchronized: create, step, destroy and the
 * setters are expected to run from one task (or under an external lock).
 */
organism_engine_t *organism_engine_create(const organism_engine_config_t *cfg);
void organism_engine_destroy(organism_engine_t *engine);

/* Clamped to 0..1; NaN/Inf is treated as 0. Copies the latest input. */
void organism_engine_set_audio(organism_engine_t *engine, const organism_audio_t *audio);

/* An active point attracts nearby particles; a strong bass beat repels them. */
void organism_engine_set_touch(organism_engine_t *engine, const organism_touch_t *touch);

/* Render one frame into the next buffer. Returns the new front index 0..2.
 * Requires cfg->buffers to be provided. */
int organism_engine_step(organism_engine_t *engine);

/* Render into a caller-provided physical buffer. src is the previous frame
 * (NULL clears to black). No engine-owned buffers are required. */
void organism_engine_step_into(organism_engine_t *engine, uint16_t *dst,
                               const uint16_t *src);
const uint16_t *organism_engine_buffer(const organism_engine_t *engine, int index);
