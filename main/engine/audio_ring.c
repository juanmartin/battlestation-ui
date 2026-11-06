#include "audio_ring.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"

#include "audio_format.h"

struct audio_ring {
    int16_t *buffer;              /* interleaved stereo, capacity_frames frames */
    uint32_t capacity_frames;     /* per channel */
    uint32_t read_pos;            /* frames, < capacity_frames */
    uint32_t write_pos;           /* frames, < capacity_frames */
    uint32_t occupied;            /* frames currently committed */
    uint32_t overruns;
    portMUX_TYPE lock;
    SemaphoreHandle_t data_available;
};

static uint32_t ring_advance(uint32_t pos, uint32_t frames, uint32_t capacity)
{
    pos += frames;
    if (pos >= capacity) {
        pos -= capacity;
    }
    return pos;
}

static void ring_copy_in(int16_t *buffer, uint32_t capacity, uint32_t start,
                         const int16_t *src, uint32_t frames)
{
    uint32_t first = capacity - start;
    if (first > frames) {
        first = frames;
    }
    memcpy(&buffer[(size_t)start * AUDIO_CHANNELS], src,
           (size_t)first * AUDIO_CHANNELS * sizeof(int16_t));
    if (frames > first) {
        memcpy(buffer, src + (size_t)first * AUDIO_CHANNELS,
               (size_t)(frames - first) * AUDIO_CHANNELS * sizeof(int16_t));
    }
}

static void ring_copy_out(const int16_t *buffer, uint32_t capacity, uint32_t start,
                          int16_t *dst, uint32_t frames)
{
    uint32_t first = capacity - start;
    if (first > frames) {
        first = frames;
    }
    memcpy(dst, &buffer[(size_t)start * AUDIO_CHANNELS],
           (size_t)first * AUDIO_CHANNELS * sizeof(int16_t));
    if (frames > first) {
        memcpy(dst + (size_t)first * AUDIO_CHANNELS, buffer,
               (size_t)(frames - first) * AUDIO_CHANNELS * sizeof(int16_t));
    }
}

audio_ring_t *audio_ring_create(uint32_t capacity_frames)
{
    if (capacity_frames == 0 ||
        capacity_frames > SIZE_MAX / (AUDIO_CHANNELS * sizeof(int16_t))) {
        return NULL;
    }

    audio_ring_t *ring = heap_caps_malloc(sizeof(*ring), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ring == NULL) {
        return NULL;
    }
    memset(ring, 0, sizeof(*ring));

    ring->buffer = heap_caps_malloc((size_t)capacity_frames * AUDIO_CHANNELS * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ring->buffer == NULL) {
        heap_caps_free(ring);
        return NULL;
    }

    ring->data_available = xSemaphoreCreateBinary();
    if (ring->data_available == NULL) {
        heap_caps_free(ring->buffer);
        heap_caps_free(ring);
        return NULL;
    }

    ring->capacity_frames = capacity_frames;
    ring->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return ring;
}

void audio_ring_destroy(audio_ring_t *ring)
{
    if (ring == NULL) {
        return;
    }
    if (ring->data_available != NULL) {
        vSemaphoreDelete(ring->data_available);
    }
    heap_caps_free(ring->buffer);
    heap_caps_free(ring);
}

size_t audio_ring_write(audio_ring_t *ring, const int16_t *frames, size_t frame_count)
{
    if (ring == NULL || frames == NULL || frame_count == 0) {
        return 0;
    }
    if (frame_count > ring->capacity_frames) {
        portENTER_CRITICAL(&ring->lock);
        ring->overruns++;
        portEXIT_CRITICAL(&ring->lock);
        return 0;
    }

    portENTER_CRITICAL(&ring->lock);
    uint32_t free_frames = ring->capacity_frames - ring->occupied;
    if (frame_count > free_frames) {
        ring->overruns++;
        portEXIT_CRITICAL(&ring->lock);
        return 0;
    }
    uint32_t start = ring->write_pos;
    portEXIT_CRITICAL(&ring->lock);

    /* The reserved span is beyond write_pos, so the consumer cannot see it. */
    ring_copy_in(ring->buffer, ring->capacity_frames, start, frames, (uint32_t)frame_count);

    portENTER_CRITICAL(&ring->lock);
    ring->write_pos = ring_advance(start, (uint32_t)frame_count, ring->capacity_frames);
    ring->occupied += (uint32_t)frame_count;
    portEXIT_CRITICAL(&ring->lock);

    xSemaphoreGive(ring->data_available);
    return frame_count;
}

size_t audio_ring_read(audio_ring_t *ring, int16_t *dst, size_t max_frames)
{
    if (ring == NULL || dst == NULL || max_frames == 0) {
        return 0;
    }

    portENTER_CRITICAL(&ring->lock);
    uint32_t frames = ring->occupied;
    if (max_frames < frames) {
        frames = (uint32_t)max_frames;
    }
    uint32_t start = ring->read_pos;
    portEXIT_CRITICAL(&ring->lock);

    if (frames == 0) {
        return 0;
    }

    ring_copy_out(ring->buffer, ring->capacity_frames, start, dst, frames);

    portENTER_CRITICAL(&ring->lock);
    ring->read_pos = ring_advance(start, frames, ring->capacity_frames);
    ring->occupied -= frames;
    portEXIT_CRITICAL(&ring->lock);

    return frames;
}

size_t audio_ring_available(audio_ring_t *ring)
{
    if (ring == NULL) {
        return 0;
    }
    portENTER_CRITICAL(&ring->lock);
    size_t available = ring->occupied;
    portEXIT_CRITICAL(&ring->lock);
    return available;
}

size_t audio_ring_free(audio_ring_t *ring)
{
    if (ring == NULL) {
        return 0;
    }
    portENTER_CRITICAL(&ring->lock);
    size_t free_frames = ring->capacity_frames - ring->occupied;
    portEXIT_CRITICAL(&ring->lock);
    return free_frames;
}

void audio_ring_reset(audio_ring_t *ring)
{
    if (ring == NULL) {
        return;
    }
    portENTER_CRITICAL(&ring->lock);
    ring->read_pos = 0;
    ring->write_pos = 0;
    ring->occupied = 0;
    portEXIT_CRITICAL(&ring->lock);

    while (xSemaphoreTake(ring->data_available, 0) == pdTRUE) {
    }
}

uint32_t audio_ring_overruns(audio_ring_t *ring)
{
    if (ring == NULL) {
        return 0;
    }
    portENTER_CRITICAL(&ring->lock);
    uint32_t overruns = ring->overruns;
    portEXIT_CRITICAL(&ring->lock);
    return overruns;
}

bool audio_ring_wait(audio_ring_t *ring, uint32_t timeout_ms)
{
    if (ring == NULL) {
        return false;
    }

    TickType_t start = xTaskGetTickCount();
    TickType_t budget = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0 && budget == 0) {
        budget = 1;
    }

    while (audio_ring_available(ring) == 0) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= budget) {
            return false;
        }
        if (xSemaphoreTake(ring->data_available, budget - elapsed) != pdTRUE) {
            return false;
        }
    }
    return true;
}
