#pragma once

#include "audio_service.h"

typedef audio_service_legacy_snapshot_t audio_probe_snapshot_t;

esp_err_t audio_probe_start(void);
void audio_probe_get(audio_probe_snapshot_t *out);
