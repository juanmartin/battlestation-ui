#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct audio_ring audio_ring_t;

/*
 * Create a single-producer/single-consumer ring of interleaved stereo frames.
 * capacity_frames is per channel. The sample buffer is allocated in PSRAM.
 * Returns NULL on allocation failure.
 */
audio_ring_t *audio_ring_create(uint32_t capacity_frames);
void audio_ring_destroy(audio_ring_t *ring);

/*
 * Producer: writes all frames or none. If the ring cannot hold the whole
 * block, the block is dropped and the overrun counter is incremented.
 * Returns the number of frames written (0 or frame_count).
 */
size_t audio_ring_write(audio_ring_t *ring, const int16_t *frames, size_t frame_count);

/*
 * Consumer: copies up to max_frames. Returns the number of frames copied.
 */
size_t audio_ring_read(audio_ring_t *ring, int16_t *dst, size_t max_frames);

size_t audio_ring_available(audio_ring_t *ring);
size_t audio_ring_free(audio_ring_t *ring);
uint32_t audio_ring_overruns(audio_ring_t *ring);

/* Discard all buffered frames and reset the ring to empty. */
void audio_ring_reset(audio_ring_t *ring);

/*
 * Block until the producer has committed data or the timeout expires.
 * Returns true if the semaphore was taken.
 */
bool audio_ring_wait(audio_ring_t *ring, uint32_t timeout_ms);
