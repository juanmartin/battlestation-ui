#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "settings_codec.h"

/*
 * Runtime settings: one versioned NVS blob, loaded before display/audio/Wi-Fi
 * start. Kconfig values are used only when the blob is absent or invalid.
 */
esp_err_t settings_init(void);

/* In-memory settings; NULL before settings_init() succeeds. */
const app_settings_t *settings_get(void);

/* Validate and copy into memory. Does not write NVS; call settings_save(). */
esp_err_t settings_update(const app_settings_t *settings);

/* Persist the current in-memory settings. */
esp_err_t settings_save(void);

/* Rebuild from Kconfig defaults and persist. */
esp_err_t settings_reset_defaults(void);
