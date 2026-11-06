#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    float dbfs_l;
    float dbfs_r;
    float dbfs_avg;
    float peak_dbfs;
    uint32_t frames;
    bool running;
    bool error;
} audio_probe_snapshot_t;

esp_err_t audio_probe_start(void);
void audio_probe_get(audio_probe_snapshot_t *out);
