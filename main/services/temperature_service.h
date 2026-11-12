#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* ESP32-P4 internal die temperature sensor (not ambient). */
esp_err_t temperature_service_init(void);
bool temperature_service_read(float *celsius);
