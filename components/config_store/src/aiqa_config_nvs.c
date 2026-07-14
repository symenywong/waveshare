#include "aiqa_config_nvs.h"

#include "nvs.h"

#include <string.h>

#define AIQA_NVS_NAMESPACE "aiqa"
#define AIQA_NVS_KEY_VOLUME "volume"
#define AIQA_NVS_KEY_ASSISTANT_NAME "assistant_name"
#define AIQA_NVS_KEY_ASSISTANT_GENDER "assistant_gender"
#define AIQA_NVS_DEFAULT_VOLUME_PERCENT 10

static esp_err_t read_optional_string(nvs_handle_t handle, const char *key, char *value, size_t value_size)
{
    size_t required = value_size;
    esp_err_t ret = nvs_get_str(handle, key, value, &required);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    return ret;
}

static esp_err_t read_optional_u8(nvs_handle_t handle, const char *key, bool *value)
{
    uint8_t stored = 0;
    esp_err_t ret = nvs_get_u8(handle, key, &stored);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        *value = stored != 0;
    }
    return ret;
}

static esp_err_t read_optional_raw_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    uint8_t stored = 0;
    esp_err_t ret = nvs_get_u8(handle, key, &stored);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        *value = stored;
    }
    return ret;
}

static esp_err_t read_optional_i32(nvs_handle_t handle, const char *key, int *value)
{
    int32_t stored = 0;
    esp_err_t ret = nvs_get_i32(handle, key, &stored);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        *value = (int)stored;
    }
    return ret;
}

static esp_err_t read_optional_version(nvs_handle_t handle, int *value)
{
    uint32_t stored = 0;
    esp_err_t ret = nvs_get_u32(handle, "version", &stored);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        *value = (int)stored;
    }
    return ret;
}

esp_err_t aiqa_config_load_from_nvs(aiqa_config_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->config = aiqa_config_default();
    snapshot->secrets = aiqa_secret_config_empty();
    snapshot->user_prefs.volume_percent = AIQA_NVS_DEFAULT_VOLUME_PERCENT;
    snapshot->user_prefs.assistant_profile = aiqa_assistant_profile_default();
    snapshot->config_status = AIQA_CONFIG_OK;
    snapshot->secret_status = AIQA_SECRET_ERR_WIFI_SSID;

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        snapshot->namespace_found = false;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    snapshot->namespace_found = true;
    ret = read_optional_version(handle, &snapshot->config.config_version);
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "provider", snapshot->config.active_provider,
                                   sizeof(snapshot->config.active_provider));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "model", snapshot->config.model,
                                   sizeof(snapshot->config.model));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "base_url", snapshot->config.base_url,
                                   sizeof(snapshot->config.base_url));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "asr_provider", snapshot->config.asr_provider,
                                   sizeof(snapshot->config.asr_provider));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "asr_model", snapshot->config.asr_model,
                                   sizeof(snapshot->config.asr_model));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "asr_base_url", snapshot->config.asr_base_url,
                                   sizeof(snapshot->config.asr_base_url));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "tts_provider", snapshot->config.tts_provider,
                                   sizeof(snapshot->config.tts_provider));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "tts_model", snapshot->config.tts_model,
                                   sizeof(snapshot->config.tts_model));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "tts_base_url", snapshot->config.tts_base_url,
                                   sizeof(snapshot->config.tts_base_url));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "tts_voice", snapshot->config.tts_voice,
                                   sizeof(snapshot->config.tts_voice));
    }
    if (ret == ESP_OK) {
        ret = read_optional_u8(handle, "stream", &snapshot->config.stream);
    }
    if (ret == ESP_OK) {
        ret = read_optional_u8(handle, "hide_reason", &snapshot->config.hide_reasoning);
    }
    if (ret == ESP_OK) {
        ret = read_optional_i32(handle, "max_tokens", &snapshot->config.max_completion_tokens);
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "wifi_ssid", snapshot->secrets.wifi_ssid,
                                   sizeof(snapshot->secrets.wifi_ssid));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "wifi_pass", snapshot->secrets.wifi_password,
                                   sizeof(snapshot->secrets.wifi_password));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "chat_key", snapshot->secrets.chat_api_key,
                                   sizeof(snapshot->secrets.chat_api_key));
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle, "asr_key", snapshot->secrets.asr_api_key,
                                   sizeof(snapshot->secrets.asr_api_key));
    }
    if (ret == ESP_OK) {
        ret = read_optional_raw_u8(handle, AIQA_NVS_KEY_VOLUME, &snapshot->user_prefs.volume_percent);
    }
    if (ret == ESP_OK) {
        ret = read_optional_string(handle,
                                   AIQA_NVS_KEY_ASSISTANT_NAME,
                                   snapshot->user_prefs.assistant_profile.name,
                                   sizeof(snapshot->user_prefs.assistant_profile.name));
    }
    if (ret == ESP_OK) {
        char gender_name[16] = {0};
        ret = read_optional_string(handle, AIQA_NVS_KEY_ASSISTANT_GENDER, gender_name, sizeof(gender_name));
        if (ret == ESP_OK && gender_name[0] != '\0') {
            snapshot->user_prefs.assistant_profile.gender =
                aiqa_assistant_gender_from_name(gender_name);
        }
    }

    nvs_close(handle);

    if (ret != ESP_OK) {
        return ret;
    }

    snapshot->config_status = aiqa_config_validate(&snapshot->config);
    snapshot->secret_status = aiqa_secret_config_validate(&snapshot->secrets);
    if (snapshot->user_prefs.volume_percent > 100) {
        snapshot->user_prefs.volume_percent = AIQA_NVS_DEFAULT_VOLUME_PERCENT;
    }
    if (!aiqa_assistant_profile_is_valid(&snapshot->user_prefs.assistant_profile)) {
        snapshot->user_prefs.assistant_profile = aiqa_assistant_profile_default();
    }
    return ESP_OK;
}

esp_err_t aiqa_config_erase_nvs_namespace(void)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t aiqa_config_save_volume_percent(uint8_t volume_percent)
{
    if (volume_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_u8(handle, AIQA_NVS_KEY_VOLUME, volume_percent);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t aiqa_config_save_assistant_profile(const aiqa_assistant_profile_t *profile)
{
    if (!aiqa_assistant_profile_is_valid(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_str(handle, AIQA_NVS_KEY_ASSISTANT_NAME, profile->name);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle,
                          AIQA_NVS_KEY_ASSISTANT_GENDER,
                          aiqa_assistant_gender_name(profile->gender));
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}
