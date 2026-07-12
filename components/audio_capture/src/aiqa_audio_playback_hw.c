#include "aiqa_audio_playback_hw.h"

#include "aiqa_audio_i2s_bus.h"
#include "board_wave_175c.h"
#include "board_wave_175c_i2c_bus.h"
#include "board_wave_175c_pins.h"

#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "aiqa_playback";

#define AIQA_AUDIO_TEST_TONE_MIN_HZ 100u
#define AIQA_AUDIO_TEST_TONE_MAX_HZ 4000u
#define AIQA_AUDIO_TEST_TONE_MAX_MS 2000u
#define AIQA_AUDIO_TEST_TONE_AMPLITUDE 16000

static esp_codec_dev_handle_t s_codec;
static aiqa_audio_playback_config_t s_config;
static bool s_started;

static esp_err_t codec_status_to_esp(int status)
{
    return status == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
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
        .rx_handle = aiqa_audio_i2s_bus_rx_handle(),
        .tx_handle = aiqa_audio_i2s_bus_tx_handle(),
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
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
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
    ESP_RETURN_ON_ERROR(aiqa_audio_i2s_bus_init(config->sample_rate_hz), TAG, "I2S0 shared bus init failed");
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
        .mclk_multiple = 0,
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

esp_err_t aiqa_audio_playback_hw_play_test_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (frequency_hz < AIQA_AUDIO_TEST_TONE_MIN_HZ ||
        frequency_hz > AIQA_AUDIO_TEST_TONE_MAX_HZ ||
        duration_ms == 0 ||
        duration_ms > AIQA_AUDIO_TEST_TONE_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_audio_playback_config_t config = aiqa_audio_playback_default_config();
    ESP_RETURN_ON_ERROR(aiqa_audio_playback_hw_init(&config), TAG, "test tone playback init failed");
    esp_err_t start_ret = aiqa_audio_playback_hw_start();
    if (start_ret != ESP_OK) {
        (void)aiqa_audio_playback_hw_stop();
        return start_ret;
    }

    int16_t samples[AIQA_AUDIO_PLAYBACK_CHUNK_BYTES / sizeof(int16_t)] = {0};
    const size_t sample_capacity = sizeof(samples) / sizeof(samples[0]);
    const size_t total_samples = ((size_t)config.sample_rate_hz * (size_t)duration_ms) / 1000u;
    const uint32_t phase_step = (uint32_t)(((uint64_t)frequency_hz << 32) / config.sample_rate_hz);
    uint32_t phase = 0;
    size_t samples_written = 0;
    esp_err_t write_ret = ESP_OK;

    while (samples_written < total_samples) {
        size_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > sample_capacity) {
            chunk_samples = sample_capacity;
        }
        for (size_t i = 0; i < chunk_samples; ++i) {
            samples[i] = (phase & 0x80000000u) ? AIQA_AUDIO_TEST_TONE_AMPLITUDE : -AIQA_AUDIO_TEST_TONE_AMPLITUDE;
            phase += phase_step;
        }
        write_ret = aiqa_audio_playback_hw_write_pcm((const uint8_t *)samples,
                                                     chunk_samples * sizeof(samples[0]));
        if (write_ret != ESP_OK) {
            break;
        }
        samples_written += chunk_samples;
    }

    esp_err_t stop_ret = aiqa_audio_playback_hw_stop();
    if (write_ret != ESP_OK) {
        return write_ret;
    }
    ESP_RETURN_ON_ERROR(stop_ret, TAG, "test tone playback stop failed");
    ESP_LOGI(TAG,
             "Playback test tone complete: %lu Hz, %lu ms, %u bytes",
             (unsigned long)frequency_hz,
             (unsigned long)duration_ms,
             (unsigned)(samples_written * sizeof(samples[0])));
    return ESP_OK;
}

bool aiqa_audio_playback_hw_is_ready(void)
{
    return s_codec != NULL;
}
