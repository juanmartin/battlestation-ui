#pragma once

#include "esp_err.h"

#include "audio_features.h"

esp_err_t audio_analysis_start(void);
esp_err_t audio_analysis_get_levels(audio_levels_t *out);
