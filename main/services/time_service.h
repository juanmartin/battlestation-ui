#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

/* Start SNTP polling with the compile-time timezone. Non-blocking: the SNTP
 * client is armed by a small task once Wi-Fi reports connected, and the clock
 * is reported unsynced until a reply sets a plausible system time. */
esp_err_t time_service_start(void);

/* True once the system clock is past 2023-11-14 (1700000000 UTC). */
bool time_service_synced(void);

/* Fill out with local time. Returns false while the clock is unsynced. */
bool time_service_get_local(struct tm *out);
