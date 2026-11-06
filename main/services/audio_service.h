#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Legacy probe snapshot kept for the M5 consumers during the M6 split. */
typedef struct {
    float dbfs_l;
    float dbfs_r;
    float dbfs_avg;
    float peak_dbfs;
    uint32_t frames;
    bool running;
    bool error;
} audio_service_legacy_snapshot_t;

esp_err_t audio_service_start(void);
void audio_service_get_legacy(audio_service_legacy_snapshot_t *out);
