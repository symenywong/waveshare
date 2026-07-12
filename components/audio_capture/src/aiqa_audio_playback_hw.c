#include "aiqa_audio_playback_hw.h"

#include "board_wave_175c.h"
#include "board_wave_175c_i2c_bus.h"
#include "board_wave_175c_pins.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "aiqa_playback";

static i2s_chan_handle_t s_i2s_tx_chan;
static esp_codec_dev_handle_t s_codec;
static aiqa_audio_playback_config_t s_config;
static bool s_started;

static esp_err_t codec_status_to_esp(int status)
{
    return status == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t init_i2s_tx(const aiqa_audio_playback_config_t *config)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_i2s_tx_chan, NULL),
                        TAG,
                        "I2S TX channel create failed");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO),
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
    std_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    return i2s_channel_init_std_mode(s_i2s_tx_chan, &std_config);
}

static esp_err_t init_codec(const aiqa_audio_playback_config_t *config)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(board_wave_175c_i2c_bus_handle(&i2c_bus), TAG, "I2C bus unavailable");

    audio_codec_i2c_cfg_t i2c_config = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    ESP_RETURN_ON_FALSE(ctrl_if != NULL, ESP_ERR_NO_MEM, TAG, "ES8311 I2C ctrl create failed");

    audio_codec_i2s_cfg_t i2s_config = {
        .port = I2S_NUM_0,
        .rx_handle = NULL,
        .tx_handle = s_i2s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_config);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_ERR_NO_MEM, TAG, "ES8311 I2S data create failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if != NULL, ESP_ERR_NO_MEM, TAG, "ES8311 GPIO interface create failed");

    es8311_codec_cfg_t es8311_config = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = WAVE_175C_PA,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .mclk_div = I2S_MCLK_MULTIPLE_256,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_config);
    ESP_RETURN_ON_FALSE(codec_if != NULL, ESP_ERR_NO_MEM, TAG, "ES8311 codec create failed");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(s_codec != NULL, ESP_ERR_NO_MEM, TAG, "ES8311 codec device create failed");
    ESP_LOGI(TAG, "ES8311 playback path initialized: %lu Hz, %u-bit, %u channel, volume=%u",
             (unsigned long)config->sample_rate_hz,
             (unsigned)config->bits_per_sample,
             (unsigned)config->channels,
             (unsigned)config->volume_percent);
    return ESP_OK;
}

esp_err_t aiqa_audio_playback_hw_init(const aiqa_audio_playback_config_t *config)
{
    if (!aiqa_audio_playback_config_is_safe(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_codec != NULL) {
        return ESP_OK;
    }

    s_config = *config;
    ESP_RETURN_ON_ERROR(init_i2s_tx(config), TAG, "I2S TX init failed");
    return init_codec(config);
}

esp_err_t aiqa_audio_playback_hw_start(void)
{
    if (s_codec == NULL) {
        aiqa_audio_playback_config_t config = aiqa_audio_playback_default_config();
        ESP_RETURN_ON_ERROR(aiqa_audio_playback_hw_init(&config), TAG, "playback init failed");
    }
    if (s_started) {
        return ESP_OK;
    }

    esp_codec_dev_sample_info_t sample_config = {
        .bits_per_sample = s_config.bits_per_sample,
        .channel = s_config.channels,
        .sample_rate = s_config.sample_rate_hz,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };
    ESP_RETURN_ON_ERROR(codec_status_to_esp(esp_codec_dev_open(s_codec, &sample_config)),
                        TAG,
                        "ES8311 open failed");
    ESP_RETURN_ON_ERROR(codec_status_to_esp(esp_codec_dev_set_out_vol(s_codec, s_config.volume_percent)),
                        TAG,
                        "ES8311 volume set failed");
    ESP_RETURN_ON_ERROR(board_wave_175c_set_pa_enabled(true), TAG, "PA enable failed");
    s_started = true;
    return ESP_OK;
}

esp_err_t aiqa_audio_playback_hw_write_pcm(const uint8_t *pcm, size_t pcm_bytes)
{
    if (!s_started || s_codec == NULL || pcm == NULL || pcm_bytes == 0 || pcm_bytes > s_config.chunk_bytes * 4) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((pcm_bytes % sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return codec_status_to_esp(esp_codec_dev_write(s_codec, (void *)pcm, (int)pcm_bytes));
}

esp_err_t aiqa_audio_playback_hw_stop(void)
{
    if (s_codec == NULL || !s_started) {
        (void)board_wave_175c_set_pa_enabled(false);
        return ESP_OK;
    }
    s_started = false;
    (void)board_wave_175c_set_pa_enabled(false);
    return codec_status_to_esp(esp_codec_dev_close(s_codec));
}

bool aiqa_audio_playback_hw_is_ready(void)
{
    return s_codec != NULL;
}
