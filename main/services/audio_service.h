#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct audio_capture_reader *audio_capture_reader_t;
typedef struct audio_playback_source *audio_playback_source_t;

typedef enum {
    AUDIO_PRIO_NOTIFICATION = 0,
    AUDIO_PRIO_METRONOME = 1,
    AUDIO_PRIO_TONE = 2,
} audio_playback_priority_t;

typedef struct {
    const char *name;
    uint32_t ring_ms; /* default 200 */
} audio_capture_config_t;

typedef struct {
    const char *name;
    audio_playback_priority_t priority;
    uint32_t ring_ms; /* default 80 */
    uint8_t channels; /* 1 or 2; mono is duplicated to stereo */
    float gain_db;    /* per-lane trim, default 0 */
} audio_playback_config_t;

typedef struct {
    bool ready;          /* capture codec opened and capture loop running */
    bool error;          /* capture init/open or last read failed */
    bool playback_ready; /* speaker opened and audio_tx started */
    bool output_muted;
    uint32_t tx_frames;
    uint32_t tx_underruns;
} audio_service_status_t;

esp_err_t audio_service_start(void);
esp_err_t audio_service_get_status(audio_service_status_t *out);

esp_err_t audio_service_capture_open(const audio_capture_config_t *cfg,
                                     audio_capture_reader_t *out);
/* One reader task per handle. Close only after that task has stopped. */
esp_err_t audio_service_capture_read(audio_capture_reader_t reader,
                                     int16_t *dst,
                                     size_t max_frames,
                                     uint32_t timeout_ms,
                                     size_t *frames_read);
esp_err_t audio_service_capture_close(audio_capture_reader_t reader);

/* One writer per lane, one lane per priority. Do not call flush/close while a
 * writer is in flight on the same handle; stop the writer first. */
esp_err_t audio_service_playback_open(const audio_playback_config_t *cfg,
                                      audio_playback_source_t *out);
esp_err_t audio_service_playback_write(audio_playback_source_t src,
                                       const int16_t *pcm,
                                       size_t frames,
                                       uint32_t timeout_ms,
                                       size_t *frames_written);
esp_err_t audio_service_playback_set_active(audio_playback_source_t src, bool active);
esp_err_t audio_service_playback_flush(audio_playback_source_t src);
esp_err_t audio_service_playback_close(audio_playback_source_t src);
esp_err_t audio_service_set_output_volume(int percent);
esp_err_t audio_service_set_output_muted(bool muted);
