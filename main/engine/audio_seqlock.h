#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Single-writer / multiple-reader seqlock for large feature snapshots.
 * The writer must own the protected data for the duration of the write.
 */
typedef struct {
    _Atomic uint32_t seq;
} audio_seqlock_t;

static inline void audio_seqlock_init(audio_seqlock_t *lock)
{
    atomic_init(&lock->seq, 0u);
}

static inline void audio_seqlock_write_begin(audio_seqlock_t *lock)
{
    atomic_fetch_add_explicit(&lock->seq, 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}

static inline void audio_seqlock_write_end(audio_seqlock_t *lock)
{
    atomic_fetch_add_explicit(&lock->seq, 1u, memory_order_release);
}

/*
 * Copy src to dst if a consistent snapshot was obtained within max_attempts.
 * On failure dst may contain a partial copy; callers that need it unchanged
 * should copy through a temporary buffer.
 */
static inline bool audio_seqlock_read(const audio_seqlock_t *lock,
                                      void *dst,
                                      const void *src,
                                      size_t size,
                                      unsigned max_attempts)
{
    for (unsigned attempt = 0; attempt < max_attempts; attempt++) {
        uint32_t before = atomic_load_explicit(&lock->seq, memory_order_acquire);
        if ((before & 1u) != 0u) {
            continue;
        }

        memcpy(dst, src, size);

        atomic_thread_fence(memory_order_acquire);
        uint32_t after = atomic_load_explicit(&lock->seq, memory_order_relaxed);
        if (before == after) {
            return true;
        }
    }
    return false;
}
