#pragma once

#include "driver/i2s_types.h"
#include "esp_err.h"

#include <stdint.h>

esp_err_t aiqa_audio_i2s_bus_init(uint32_t sample_rate_hz);
i2s_chan_handle_t aiqa_audio_i2s_bus_tx_handle(void);
i2s_chan_handle_t aiqa_audio_i2s_bus_rx_handle(void);
