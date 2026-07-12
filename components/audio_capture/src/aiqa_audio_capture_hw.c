#include "aiqa_audio_capture_hw.h"

#include "aiqa_audio_i2s_bus.h"
#include "board_wave_175c_i2c_bus.h"
#include "board_wave_175c_pins.h"

#include "driver/i2s_common.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "aiqa_audio_hw";

static esp_codec_dev_handle_t s_codec;
static aiqa_audio_capture_hw_config_t s_config;
static bool s_started;
static int16_t s_tdm_samples[AIQA_AUDIO_CAPTURE_CHUNK_FRAMES * AIQA_AUDIO_CAPTURE_ES7210_SOURCE_CHANNELS];

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static esp_err_t codec_status_to_esp(int status)
{
    return status == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static int16_t abs_i16(int16_t value)
{
    if (value == INT16_MIN) {
        return INT16_MAX;
    }
    return value < 0 ? (int16_t)-value : value;
}

static esp_err_t init_codec(const aiqa_audio_capture_hw_config_t *config)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(board_wave_175c_i2c_bus_handle(&i2c_bus), TAG, "I2C bus unavailable");

    audio_codec_i2c_cfg_t i2c_config = {
        .port = I2C_NUM_0,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    ESP_RETURN_ON_FALSE(ctrl_if != NULL, ESP_ERR_NO_MEM, TAG, "ES7210 I2C ctrl create failed");

    audio_codec_i2s_cfg_t i2s_config = {
        .port = I2S_NUM_0,
        .rx_handle = aiqa_audio_i2s_bus_rx_handle(),
        .tx_handle = aiqa_audio_i2s_bus_tx_handle(),
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_config);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_ERR_NO_MEM, TAG, "ES7210 I2S data create failed");

    es7210_codec_cfg_t es7210_config = {
        .ctrl_if = ctrl_if,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = I2S_MCLK_MULTIPLE_256,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&es7210_config);
    ESP_RETURN_ON_FALSE(codec_if != NULL, ESP_ERR_NO_MEM, TAG, "ES7210 codec create failed");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(s_codec != NULL, ESP_ERR_NO_MEM, TAG, "ES7210 codec device create failed");
    ESP_LOGI(TAG, "ES7210 capture path initialized: %lu Hz, %u-bit, %u->%u channel",
             (unsigned long)config->sample_rate_hz,
             (unsigned)config->bits_per_sample,
             (unsigned)config->source_channels,
             (unsigned)config->output_channels);
    return ESP_OK;
}

esp_err_t aiqa_audio_capture_hw_init(const aiqa_audio_capture_hw_config_t *config)
{
    if (!aiqa_audio_capture_hw_config_is_safe(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_codec != NULL) {
        return ESP_OK;
    }

    s_config = *config;
    ESP_RETURN_ON_ERROR(aiqa_audio_i2s_bus_init(config->sample_rate_hz), TAG, "I2S0 shared bus init failed");
    return init_codec(config);
}

esp_err_t aiqa_audio_capture_hw_start(void)
{
    if (s_codec == NULL) {
        aiqa_audio_capture_hw_config_t config = aiqa_audio_capture_hw_default_config();
        ESP_RETURN_ON_ERROR(aiqa_audio_capture_hw_init(&config), TAG, "capture init failed");
    }
    if (s_started) {
        return ESP_OK;
    }

    esp_codec_dev_sample_info_t sample_config = {
        .bits_per_sample = s_config.bits_per_sample,
        .channel = s_config.source_channels,
        .channel_mask = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
        .sample_rate = s_config.sample_rate_hz,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(codec_status_to_esp(esp_codec_dev_open(s_codec, &sample_config)),
                        TAG,
                        "ES7210 open failed");
    ESP_RETURN_ON_ERROR(codec_status_to_esp(esp_codec_dev_set_in_gain(s_codec, s_config.mic_gain_db)),
                        TAG,
                        "ES7210 gain set failed");
    s_started = true;
    return ESP_OK;
}

esp_err_t aiqa_audio_capture_hw_read_mono(
    int16_t *out_samples,
    size_t out_sample_count,
    aiqa_audio_capture_stats_t *stats)
{
    if (!s_started || s_codec == NULL || out_samples == NULL || out_sample_count == 0 ||
        out_sample_count > AIQA_AUDIO_CAPTURE_CHUNK_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t raw_samples = out_sample_count * s_config.source_channels;
    const size_t raw_bytes = raw_samples * sizeof(s_tdm_samples[0]);
    (void)memset(s_tdm_samples, 0, raw_bytes);
    size_t bytes_read = 0;
    ESP_RETURN_ON_ERROR(i2s_channel_read(aiqa_audio_i2s_bus_rx_handle(),
                                         s_tdm_samples,
                                         raw_bytes,
                                         &bytes_read,
                                         pdMS_TO_TICKS(s_config.read_timeout_ms)),
                        TAG,
                        "ES7210 I2S read failed");

    const size_t complete_frames = bytes_read / (sizeof(s_tdm_samples[0]) * s_config.source_channels);
    aiqa_audio_capture_stats_t local_stats = {
        .bytes_read = bytes_read,
        .mono_samples = complete_frames,
        .peak_abs = 0,
        .has_signal = false,
    };
    for (size_t index = 0; index < complete_frames; ++index) {
        const int16_t sample = s_tdm_samples[index * s_config.source_channels];
        const int16_t peak = abs_i16(sample);
        out_samples[index] = sample;
        if (peak > local_stats.peak_abs) {
            local_stats.peak_abs = peak;
        }
    }
    local_stats.has_signal = local_stats.peak_abs > 32;
    if (stats != NULL) {
        *stats = local_stats;
    }
    secure_zero(s_tdm_samples, raw_bytes);
    return ESP_OK;
}

esp_err_t aiqa_audio_capture_hw_stop(void)
{
    if (s_codec == NULL || !s_started) {
        return ESP_OK;
    }
    s_started = false;
    return codec_status_to_esp(esp_codec_dev_close(s_codec));
}

bool aiqa_audio_capture_hw_is_ready(void)
{
    return s_codec != NULL;
}
