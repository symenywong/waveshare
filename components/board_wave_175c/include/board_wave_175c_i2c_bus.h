#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_wave_175c_i2c_bus_handle(i2c_master_bus_handle_t *out_bus);

#ifdef __cplusplus
}
#endif
