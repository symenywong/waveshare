#include "aiqa_config_nvs.h"
#include "aiqa_config_nvs_internal.h"
#include "aiqa_config_transaction_internal.h"

#include "nvs.h"

#include <string.h>

#define AIQA_NVS_NAMESPACE "aiqa"
#define AIQA_NVS_KEY_VOLUME "volume"
#define AIQA_NVS_KEY_ASSISTANT_NAME "assistant_name"
#define AIQA_NVS_KEY_ASSISTANT_GENDER "assist_gender"
#define AIQA_NVS_KEY_DIALOGUE_LANGUAGE "dialogue_lang"
#define AIQA_NVS_DEFAULT_VOLUME_PERCENT 10

_Static_assert(sizeof(AIQA_NVS_KEY_VOLUME) <= 16, "NVS keys are limited to 15 characters");
_Static_assert(sizeof(AIQA_NVS_KEY_ASSISTANT_NAME) <= 16, "NVS keys are limited to 15 characters");
_Static_assert(sizeof(AIQA_NVS_KEY_ASSISTANT_GENDER) <= 16, "NVS keys are limited to 15 characters");
_Static_assert(sizeof(AIQA_NVS_KEY_DIALOGUE_LANGUAGE) <= 16, "NVS keys are limited to 15 characters");

static bool stored_volume_matches(uint8_t expected)
{
    nvs_handle_t handle = 0;
    uint8_t stored = 0;
    if (nvs_open(AIQA_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    const esp_err_t ret = nvs_get_u8(handle, AIQA_NVS_KEY_VOLUME, &stored);
    nvs_close(handle);
    return ret == ESP_OK && stored == expected;
}

static bool stored_profile_matches(const aiqa_assistant_profile_t *expected)
{
    nvs_handle_t handle = 0;
    if (nvs_open(AIQA_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    char name[AIQA_ASSISTANT_NAME_MAX_LEN] = {0};
    char gender[16] = {0};
    size_t name_size = sizeof(name);
    size_t gender_size = sizeof(gender);
    const esp_err_t name_ret =
        nvs_get_str(handle, AIQA_NVS_KEY_ASSISTANT_NAME, name, &name_size);
    const esp_err_t gender_ret =
        nvs_get_str(handle, AIQA_NVS_KEY_ASSISTANT_GENDER, gender, &gender_size);
    nvs_close(handle);
    return name_ret == ESP_OK &&
           gender_ret == ESP_OK &&
           strcmp(name, expected->name) == 0 &&
           strcmp(gender, aiqa_assistant_gender_name(expected->gender)) == 0;
}

static bool stored_language_matches(aiqa_dialogue_language_t expected)
{
    nvs_handle_t handle = 0;
    if (nvs_open(AIQA_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    char language_code[8] = {0};
    size_t language_size = sizeof(language_code);
    const esp_err_t ret = nvs_get_str(
        handle, AIQA_NVS_KEY_DIALOGUE_LANGUAGE, language_code, &language_size);
    nvs_close(handle);
    return ret == ESP_OK &&
           strcmp(language_code, aiqa_language_chat_code(expected)) == 0;
}

void aiqa_config_snapshot_secure_clear(aiqa_config_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    volatile unsigned char *bytes = (volatile unsigned char *)snapshot;
    size_t remaining = sizeof(*snapshot);
    while (remaining > 0) {
        *bytes++ = 0;
        --remaining;
    }
}

static esp_err_t read_optional_string(nvs_handle_t handle, const char *key, char *value, size_t value_size)
{
    size_t required = value_size;
    esp_err_t ret = nvs_get_str(handle, key, value, &required);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    return ret;
}

static esp_err_t read_optional_dialogue_language(
    nvs_handle_t handle,
    aiqa_dialogue_language_t *out_language)
{
    if (out_language == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_language = aiqa_language_default();
    char language_code[8] = {0};
    size_t required = sizeof(language_code);
    const esp_err_t ret = nvs_get_str(
        handle, AIQA_NVS_KEY_DIALOGUE_LANGUAGE, language_code, &required);
    if (ret == ESP_ERR_NVS_NOT_FOUND ||
        ret == ESP_ERR_NVS_INVALID_LENGTH ||
        ret == ESP_ERR_NVS_TYPE_MISMATCH) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    (void)aiqa_language_from_chat_code(language_code, out_language);
    return ESP_OK;
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

static esp_err_t read_optional_version(nvs_handle_t handle, int *value, bool *found)
{
    uint32_t stored = 0;
    esp_err_t ret = nvs_get_u32(handle, "version", &stored);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        *value = (int)stored;
        *found = true;
    }
    return ret;
}

esp_err_t aiqa_config_load_legacy_from_nvs(aiqa_config_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->config = aiqa_config_default();
    snapshot->secrets = aiqa_secret_config_empty();
    snapshot->user_prefs.volume_percent = AIQA_NVS_DEFAULT_VOLUME_PERCENT;
    snapshot->user_prefs.assistant_profile = aiqa_assistant_profile_default();
    snapshot->user_prefs.dialogue_language = aiqa_language_default();
    snapshot->config_status = AIQA_CONFIG_OK;
    snapshot->secret_status = AIQA_SECRET_ERR_WIFI_SSID;
    snapshot->revision = 1;
    snapshot->active_slot = AIQA_CONFIG_SLOT_LEGACY;

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        snapshot->namespace_found = false;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    snapshot->namespace_found = false;
    ret = read_optional_version(
        handle, &snapshot->config.config_version, &snapshot->namespace_found);
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
    if (ret == ESP_OK) {
        ret = read_optional_dialogue_language(
            handle, &snapshot->user_prefs.dialogue_language);
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
    if (!aiqa_language_is_valid(snapshot->user_prefs.dialogue_language)) {
        snapshot->user_prefs.dialogue_language = aiqa_language_default();
    }
    return ESP_OK;
}

esp_err_t aiqa_config_erase_legacy_nvs_namespace(void)
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

esp_err_t aiqa_config_erase_legacy_record_from_nvs(void)
{
    static const char *const record_keys[] = {
        "version", "provider", "model", "base_url", "asr_provider",
        "asr_model", "asr_base_url", "tts_provider", "tts_model",
        "tts_base_url", "tts_voice", "stream", "hide_reason",
        "max_tokens", "wifi_ssid", "wifi_pass", "chat_key", "asr_key",
    };
    static const char *const secret_keys[] = {
        "wifi_ssid", "wifi_pass", "chat_key", "asr_key",
    };
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (ret != ESP_OK) return ret;
    bool changed = false;
    for (size_t index = 0;
         index < sizeof(record_keys) / sizeof(record_keys[0]);
         ++index) {
        ret = nvs_erase_key(handle, record_keys[index]);
        if (ret == ESP_OK) changed = true;
        else if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
        if (ret != ESP_OK) break;
    }
    if (ret == ESP_OK && changed) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (ret != ESP_OK) return ret;
    for (size_t index = 0;
         index < sizeof(secret_keys) / sizeof(secret_keys[0]);
         ++index) {
        size_t required = 0U;
        ret = nvs_get_str(handle, secret_keys[index], NULL, &required);
        if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
        else if (ret == ESP_OK) ret = ESP_ERR_INVALID_STATE;
        if (ret != ESP_OK) break;
    }
    nvs_close(handle);
    return ret;
}

esp_err_t aiqa_config_load_legacy_prefs_from_nvs(aiqa_user_prefs_t *prefs)
{
    if (prefs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    prefs->volume_percent = AIQA_NVS_DEFAULT_VOLUME_PERCENT;
    prefs->assistant_profile = aiqa_assistant_profile_default();
    prefs->dialogue_language = aiqa_language_default();

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    ret = read_optional_raw_u8(handle, AIQA_NVS_KEY_VOLUME, &prefs->volume_percent);
    if (ret == ESP_OK) {
        ret = read_optional_string(
            handle,
            AIQA_NVS_KEY_ASSISTANT_NAME,
            prefs->assistant_profile.name,
            sizeof(prefs->assistant_profile.name));
    }
    if (ret == ESP_OK) {
        char gender_name[16] = {0};
        ret = read_optional_string(
            handle, AIQA_NVS_KEY_ASSISTANT_GENDER, gender_name, sizeof(gender_name));
        if (ret == ESP_OK && gender_name[0] != '\0') {
            prefs->assistant_profile.gender = aiqa_assistant_gender_from_name(gender_name);
        }
    }
    if (ret == ESP_OK) {
        ret = read_optional_dialogue_language(handle, &prefs->dialogue_language);
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }
    if (prefs->volume_percent > 100) {
        prefs->volume_percent = AIQA_NVS_DEFAULT_VOLUME_PERCENT;
    }
    if (!aiqa_assistant_profile_is_valid(&prefs->assistant_profile)) {
        prefs->assistant_profile = aiqa_assistant_profile_default();
    }
    if (!aiqa_language_is_valid(prefs->dialogue_language)) {
        prefs->dialogue_language = aiqa_language_default();
    }
    return ESP_OK;
}

esp_err_t aiqa_config_save_volume_percent(uint8_t volume_percent)
{
    if (volume_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!aiqa_config_lifecycle_try_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        aiqa_config_lifecycle_unlock();
        return ret;
    }
    ret = nvs_set_u8(handle, AIQA_NVS_KEY_VOLUME, volume_percent);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret != ESP_OK && stored_volume_matches(volume_percent)) {
        ret = ESP_OK;
    }
    aiqa_config_lifecycle_unlock();
    return ret;
}

esp_err_t aiqa_config_save_assistant_profile(const aiqa_assistant_profile_t *profile)
{
    if (!aiqa_assistant_profile_is_valid(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!aiqa_config_lifecycle_try_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        aiqa_config_lifecycle_unlock();
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
    if (ret != ESP_OK && stored_profile_matches(profile)) {
        ret = ESP_OK;
    }
    aiqa_config_lifecycle_unlock();
    return ret;
}

esp_err_t aiqa_config_save_dialogue_language(aiqa_dialogue_language_t language)
{
    if (!aiqa_language_is_valid(language)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!aiqa_config_lifecycle_try_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        aiqa_config_lifecycle_unlock();
        return ret;
    }
    ret = nvs_set_str(
        handle, AIQA_NVS_KEY_DIALOGUE_LANGUAGE, aiqa_language_chat_code(language));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret != ESP_OK && stored_language_matches(language)) {
        ret = ESP_OK;
    }
    aiqa_config_lifecycle_unlock();
    return ret;
}
