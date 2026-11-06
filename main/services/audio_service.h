#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct audio_capture_reader *audio_capture_reader_t;

typedef struct {
    const char *name;
    uint32_t ring_ms; /* default 200 */
} audio_capture_config_t;

typedef struct {
    bool ready; /* codec opened and capture loop running */
    bool error; /* init/open failed or the last read failed */
} audio_service_status_t;

esp_err_t audio_service_start(void);
esp_err_t audio_service_get_status(audio_service_status_t *out);

esp_err_t audio_service_capture_open(const audio_capture_config_t *cfg,
                                     audio_capture_reader_t *out);
esp_err_t audio_service_capture_read(audio_capture_reader_t reader,
                                     int16_t *dst,
                                     size_t max_frames,
                                     uint32_t timeout_ms,
                                     size_t *frames_read);
esp_err_t audio_service_capture_close(audio_capture_reader_t reader);
