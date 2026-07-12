#include "aiqa_audio_i2s_bus.h"

#include "board_wave_175c_pins.h"

#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "aiqa_i2s_bus";

static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;
static uint32_t s_sample_rate_hz;

esp_err_t aiqa_audio_i2s_bus_init(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tx_chan != NULL && s_rx_chan != NULL) {
        return s_sample_rate_hz == sample_rate_hz ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    i2s_chan_config_t channel_config = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_tx_chan, &s_rx_chan),
                        TAG,
                        "I2S0 channel pair create failed");

    i2s_std_config_t tx_config = {
        .clk_cfg = {
            .sample_rate_hz = sample_rate_hz,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = WAVE_175C_ES7210_MCLK,
            .bclk = WAVE_175C_ES7210_BCLK,
            .ws = WAVE_175C_ES7210_LRCK,
            .dout = WAVE_175C_ES8311_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &tx_config),
                        TAG,
                        "I2S0 TX STD init failed");

    i2s_tdm_config_t rx_config = {
        .clk_cfg = {
            .sample_rate_hz = sample_rate_hz,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = WAVE_175C_ES7210_MCLK,
            .bclk = WAVE_175C_ES7210_BCLK,
            .ws = WAVE_175C_ES7210_LRCK,
            .dout = I2S_GPIO_UNUSED,
            .din = WAVE_175C_ES7210_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_rx_chan, &rx_config),
                        TAG,
                        "I2S0 RX TDM init failed");

    s_sample_rate_hz = sample_rate_hz;
    ESP_LOGI(TAG,
             "I2S0 shared bus initialized: rate=%lu MCLK=%d BCLK=%d LRCK=%d DOUT=%d DIN=%d",
             (unsigned long)sample_rate_hz,
             WAVE_175C_ES7210_MCLK,
             WAVE_175C_ES7210_BCLK,
             WAVE_175C_ES7210_LRCK,
             WAVE_175C_ES8311_DOUT,
             WAVE_175C_ES7210_DIN);
    return ESP_OK;
}

i2s_chan_handle_t aiqa_audio_i2s_bus_tx_handle(void)
{
    return s_tx_chan;
}

i2s_chan_handle_t aiqa_audio_i2s_bus_rx_handle(void)
{
    return s_rx_chan;
}
