#include "aiqa_config_nvs.h"
#include "aiqa_config_nvs_internal.h"
#include "aiqa_config_transaction_internal.h"

#include "nvs.h"

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

#define AIQA_NVS_NAMESPACE_LEGACY "aiqa"
#define AIQA_NVS_NAMESPACE_SLOT_A "aiqa_cfg_a"
#define AIQA_NVS_NAMESPACE_SLOT_B "aiqa_cfg_b"
#define AIQA_NVS_NAMESPACE_META "aiqa_meta"
#define AIQA_NVS_KEY_LAYOUT "layout"
#define AIQA_NVS_KEY_REVISION "revision"
#define AIQA_NVS_KEY_HEAD "head"
#define AIQA_NVS_KEY_RESET "reset"
#define AIQA_NVS_LAYOUT_VERSION 1U

static esp_err_t complete_pending_reset(void);
static esp_err_t complete_pending_reset_locked(void);
static atomic_bool s_storage_operation_locked = ATOMIC_VAR_INIT(false);

static bool try_lock_storage(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong_explicit(
        &s_storage_operation_locked,
        &expected,
        true,
        memory_order_acquire,
        memory_order_relaxed);
}

static void unlock_storage(void)
{
    atomic_store_explicit(&s_storage_operation_locked, false, memory_order_release);
}

static const char *slot_namespace(aiqa_config_slot_t slot)
{
    if (slot == AIQA_CONFIG_SLOT_A) {
        return AIQA_NVS_NAMESPACE_SLOT_A;
    }
    if (slot == AIQA_CONFIG_SLOT_B) {
        return AIQA_NVS_NAMESPACE_SLOT_B;
    }
    return NULL;
}

static uint64_t encode_head(aiqa_config_slot_t slot, uint32_t revision)
{
    const uint64_t slot_bit = slot == AIQA_CONFIG_SLOT_B ? 1U : 0U;
    return ((uint64_t)revision << 1U) | slot_bit;
}

static bool decode_head(uint64_t head, aiqa_config_slot_t *slot, uint32_t *revision)
{
    const uint64_t stored_revision = head >> 1U;
    if (slot == NULL || revision == NULL || stored_revision == 0 || stored_revision > UINT32_MAX) {
        return false;
    }
    *slot = (head & 1U) == 0 ? AIQA_CONFIG_SLOT_A : AIQA_CONFIG_SLOT_B;
    *revision = (uint32_t)stored_revision;
    return true;
}

static esp_err_t read_required_string(
    nvs_handle_t handle,
    const char *key,
    char *value,
    size_t value_size)
{
    size_t required = value_size;
    return nvs_get_str(handle, key, value, &required);
}

static esp_err_t write_record_fields(nvs_handle_t handle, const aiqa_config_record_t *record)
{
    esp_err_t ret = nvs_set_u32(handle, AIQA_NVS_KEY_LAYOUT, AIQA_NVS_LAYOUT_VERSION);
    if (ret == ESP_OK) {
        ret = nvs_set_u32(handle, AIQA_NVS_KEY_REVISION, record->revision);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(handle, "version", (uint32_t)record->config.config_version);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "provider", record->config.active_provider);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "model", record->config.model);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "base_url", record->config.base_url);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "asr_provider", record->config.asr_provider);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "asr_model", record->config.asr_model);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "asr_base_url", record->config.asr_base_url);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "tts_provider", record->config.tts_provider);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "tts_model", record->config.tts_model);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "tts_base_url", record->config.tts_base_url);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "tts_voice", record->config.tts_voice);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, "stream", record->config.stream ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, "hide_reason", record->config.hide_reasoning ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, "max_tokens", record->config.max_completion_tokens);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "wifi_ssid", record->secrets.wifi_ssid);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "wifi_pass", record->secrets.wifi_password);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "chat_key", record->secrets.chat_api_key);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "asr_key", record->secrets.asr_api_key);
    }
    return ret;
}

static esp_err_t read_record_fields(
    nvs_handle_t handle,
    aiqa_config_slot_t slot,
    aiqa_config_record_t *record)
{
    aiqa_config_record_t loaded = {.active_slot = slot};
    uint32_t layout = 0;
    uint32_t version = 0;
    uint8_t boolean_value = 0;
    int32_t integer_value = 0;
    esp_err_t ret = nvs_get_u32(handle, AIQA_NVS_KEY_LAYOUT, &layout);
    if (ret == ESP_OK && layout != AIQA_NVS_LAYOUT_VERSION) {
        ret = ESP_ERR_INVALID_STATE;
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u32(handle, AIQA_NVS_KEY_REVISION, &loaded.revision);
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u32(handle, "version", &version);
        loaded.config.config_version = (int)version;
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "provider", loaded.config.active_provider,
                                   sizeof(loaded.config.active_provider));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "model", loaded.config.model, sizeof(loaded.config.model));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "base_url", loaded.config.base_url,
                                   sizeof(loaded.config.base_url));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "asr_provider", loaded.config.asr_provider,
                                   sizeof(loaded.config.asr_provider));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "asr_model", loaded.config.asr_model,
                                   sizeof(loaded.config.asr_model));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "asr_base_url", loaded.config.asr_base_url,
                                   sizeof(loaded.config.asr_base_url));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "tts_provider", loaded.config.tts_provider,
                                   sizeof(loaded.config.tts_provider));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "tts_model", loaded.config.tts_model,
                                   sizeof(loaded.config.tts_model));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "tts_base_url", loaded.config.tts_base_url,
                                   sizeof(loaded.config.tts_base_url));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "tts_voice", loaded.config.tts_voice,
                                   sizeof(loaded.config.tts_voice));
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, "stream", &boolean_value);
        loaded.config.stream = boolean_value != 0;
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, "hide_reason", &boolean_value);
        loaded.config.hide_reasoning = boolean_value != 0;
    }
    if (ret == ESP_OK) {
        ret = nvs_get_i32(handle, "max_tokens", &integer_value);
        loaded.config.max_completion_tokens = (int)integer_value;
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "wifi_ssid", loaded.secrets.wifi_ssid,
                                   sizeof(loaded.secrets.wifi_ssid));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "wifi_pass", loaded.secrets.wifi_password,
                                   sizeof(loaded.secrets.wifi_password));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "chat_key", loaded.secrets.chat_api_key,
                                   sizeof(loaded.secrets.chat_api_key));
    }
    if (ret == ESP_OK) {
        ret = read_required_string(handle, "asr_key", loaded.secrets.asr_api_key,
                                   sizeof(loaded.secrets.asr_api_key));
    }
    if (ret == ESP_OK &&
        (loaded.revision == 0 ||
         aiqa_config_validate(&loaded.config) != AIQA_CONFIG_OK ||
         aiqa_secret_config_validate(&loaded.secrets) != AIQA_SECRET_OK)) {
        ret = ESP_ERR_INVALID_STATE;
    }
    if (ret == ESP_OK) {
        *record = loaded;
    }
    aiqa_config_record_secure_clear(&loaded);
    return ret;
}

static esp_err_t load_slot_record(aiqa_config_slot_t slot, aiqa_config_record_t *record)
{
    const char *namespace_name = slot_namespace(slot);
    if (namespace_name == NULL || record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = read_record_fields(handle, slot, record);
    nvs_close(handle);
    return ret;
}

static esp_err_t slot_record_marker_exists(aiqa_config_slot_t slot, bool *exists)
{
    const char *namespace_name = slot_namespace(slot);
    if (namespace_name == NULL || exists == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *exists = false;
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    uint32_t layout = 0;
    ret = nvs_get_u32(handle, AIQA_NVS_KEY_LAYOUT, &layout);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    *exists = true;
    return ESP_OK;
}

static bool config_equals(const aiqa_config_t *left, const aiqa_config_t *right)
{
    return left->config_version == right->config_version &&
           left->stream == right->stream &&
           left->hide_reasoning == right->hide_reasoning &&
           left->max_completion_tokens == right->max_completion_tokens &&
           strcmp(left->active_provider, right->active_provider) == 0 &&
           strcmp(left->model, right->model) == 0 &&
           strcmp(left->base_url, right->base_url) == 0 &&
           strcmp(left->asr_provider, right->asr_provider) == 0 &&
           strcmp(left->asr_model, right->asr_model) == 0 &&
           strcmp(left->asr_base_url, right->asr_base_url) == 0 &&
           strcmp(left->tts_provider, right->tts_provider) == 0 &&
           strcmp(left->tts_model, right->tts_model) == 0 &&
           strcmp(left->tts_base_url, right->tts_base_url) == 0 &&
           strcmp(left->tts_voice, right->tts_voice) == 0;
}

static bool secrets_equal(const aiqa_secret_config_t *left, const aiqa_secret_config_t *right)
{
    return strcmp(left->wifi_ssid, right->wifi_ssid) == 0 &&
           strcmp(left->wifi_password, right->wifi_password) == 0 &&
           strcmp(left->chat_api_key, right->chat_api_key) == 0 &&
           strcmp(left->asr_api_key, right->asr_api_key) == 0;
}

static bool records_equal(const aiqa_config_record_t *left, const aiqa_config_record_t *right)
{
    return left->revision == right->revision &&
           left->active_slot == right->active_slot &&
           config_equals(&left->config, &right->config) &&
           secrets_equal(&left->secrets, &right->secrets);
}

static esp_err_t read_head(uint64_t *head)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE_META, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_get_u64(handle, AIQA_NVS_KEY_HEAD, head);
    nvs_close(handle);
    return ret;
}

static esp_err_t reset_is_pending(bool *pending)
{
    *pending = false;
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE_META, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    uint8_t reset = 0;
    ret = nvs_get_u8(handle, AIQA_NVS_KEY_RESET, &reset);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        *pending = reset != 0;
    }
    return ret;
}

static void initialize_empty_snapshot(aiqa_config_snapshot_t *snapshot)
{
    aiqa_config_snapshot_secure_clear(snapshot);
    snapshot->config = aiqa_config_default();
    snapshot->secrets = aiqa_secret_config_empty();
    snapshot->user_prefs.volume_percent = 10;
    snapshot->user_prefs.assistant_profile = aiqa_assistant_profile_default();
    snapshot->user_prefs.dialogue_language = aiqa_language_default();
    snapshot->config_status = AIQA_CONFIG_OK;
    snapshot->secret_status = AIQA_SECRET_ERR_WIFI_SSID;
    snapshot->revision = 1;
    snapshot->active_slot = AIQA_CONFIG_SLOT_LEGACY;
    snapshot->namespace_found = false;
}

static esp_err_t load_active_record_unlocked(aiqa_config_record_t *record, bool *found)
{
    if (record == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = false;
    aiqa_config_record_secure_clear(record);

    bool reset_pending = false;
    esp_err_t ret = reset_is_pending(&reset_pending);
    if (ret != ESP_OK) {
        return ret;
    }
    if (reset_pending) {
        return complete_pending_reset();
    }

    uint64_t head = 0;
    ret = read_head(&head);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        bool slot_a_exists = false;
        bool slot_b_exists = false;
        ret = slot_record_marker_exists(AIQA_CONFIG_SLOT_A, &slot_a_exists);
        if (ret == ESP_OK) {
            ret = slot_record_marker_exists(AIQA_CONFIG_SLOT_B, &slot_b_exists);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        if (slot_a_exists || slot_b_exists) {
            return ESP_ERR_INVALID_STATE;
        }

        aiqa_config_snapshot_t legacy;
        ret = aiqa_config_load_legacy_from_nvs(&legacy);
        if (ret != ESP_OK || !legacy.namespace_found) {
            aiqa_config_snapshot_secure_clear(&legacy);
            return ret;
        }
        record->config = legacy.config;
        record->secrets = legacy.secrets;
        record->revision = 1;
        record->active_slot = AIQA_CONFIG_SLOT_LEGACY;
        *found = true;
        if (legacy.config_status == AIQA_CONFIG_OK && legacy.secret_status == AIQA_SECRET_OK) {
            aiqa_config_record_t migrated = *record;
            migrated.active_slot = AIQA_CONFIG_SLOT_A;
            const esp_err_t stage_ret =
                aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &migrated);
            if (stage_ret == ESP_OK) {
                const aiqa_config_activation_result_t activation =
                    aiqa_config_nvs_activate_record(
                        &migrated, AIQA_CONFIG_SLOT_LEGACY, record->revision);
                if (activation == AIQA_CONFIG_ACTIVATION_COMMITTED) {
                    *record = migrated;
                    if (!aiqa_config_nvs_discard_record(
                            AIQA_CONFIG_SLOT_LEGACY)) {
                        aiqa_config_record_secure_clear(&migrated);
                        aiqa_config_record_secure_clear(record);
                        aiqa_config_snapshot_secure_clear(&legacy);
                        *found = false;
                        return ESP_ERR_INVALID_STATE;
                    }
                } else if (activation == AIQA_CONFIG_ACTIVATION_INDETERMINATE) {
                    aiqa_config_record_secure_clear(&migrated);
                    aiqa_config_record_secure_clear(record);
                    aiqa_config_snapshot_secure_clear(&legacy);
                    *found = false;
                    return ESP_ERR_INVALID_STATE;
                } else if (!aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_A)) {
                    aiqa_config_record_secure_clear(&migrated);
                    aiqa_config_record_secure_clear(record);
                    aiqa_config_snapshot_secure_clear(&legacy);
                    *found = false;
                    return ESP_ERR_INVALID_STATE;
                }
            } else if (!aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_A)) {
                aiqa_config_record_secure_clear(&migrated);
                aiqa_config_record_secure_clear(record);
                aiqa_config_snapshot_secure_clear(&legacy);
                *found = false;
                return ESP_ERR_INVALID_STATE;
            }
            aiqa_config_record_secure_clear(&migrated);
        }
        aiqa_config_snapshot_secure_clear(&legacy);
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    aiqa_config_slot_t slot = AIQA_CONFIG_SLOT_LEGACY;
    uint32_t revision = 0;
    if (!decode_head(head, &slot, &revision)) {
        return ESP_ERR_INVALID_STATE;
    }
    ret = load_slot_record(slot, record);
    if (ret != ESP_OK) {
        return ret;
    }
    if (record->revision != revision) {
        aiqa_config_record_secure_clear(record);
        return ESP_ERR_INVALID_STATE;
    }
    if (!aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_LEGACY)) {
        aiqa_config_record_secure_clear(record);
        return ESP_ERR_INVALID_STATE;
    }
    *found = true;
    return ESP_OK;
}

esp_err_t aiqa_config_nvs_load_active_record(aiqa_config_record_t *record, bool *found)
{
    if (!aiqa_config_lifecycle_try_lock()) {
        if (record != NULL) {
            aiqa_config_record_secure_clear(record);
        }
        if (found != NULL) {
            *found = false;
        }
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t ret = load_active_record_unlocked(record, found);
    aiqa_config_lifecycle_unlock();
    return ret;
}

static esp_err_t load_snapshot_unlocked(aiqa_config_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    aiqa_config_snapshot_secure_clear(snapshot);
    bool reset_pending = false;
    esp_err_t ret = reset_is_pending(&reset_pending);
    if (ret != ESP_OK) {
        return ret;
    }
    if (reset_pending) {
        initialize_empty_snapshot(snapshot);
        return complete_pending_reset();
    }

    uint64_t current_head = 0;
    const esp_err_t head_ret = read_head(&current_head);
    if (head_ret == ESP_OK) {
        initialize_empty_snapshot(snapshot);
        ret = aiqa_config_load_legacy_prefs_from_nvs(&snapshot->user_prefs);
    } else if (head_ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = aiqa_config_load_legacy_from_nvs(snapshot);
    } else {
        return head_ret;
    }
    if (ret != ESP_OK) {
        aiqa_config_snapshot_secure_clear(snapshot);
        return ret;
    }

    aiqa_config_record_t active = {0};
    bool found = false;
    ret = load_active_record_unlocked(&active, &found);
    if (ret != ESP_OK) {
        aiqa_config_record_secure_clear(&active);
        aiqa_config_snapshot_secure_clear(snapshot);
        return ret;
    }
    if (found) {
        snapshot->config = active.config;
        snapshot->secrets = active.secrets;
        snapshot->revision = active.revision;
        snapshot->active_slot = active.active_slot;
        snapshot->namespace_found = true;
        snapshot->config_status = aiqa_config_validate(&snapshot->config);
        snapshot->secret_status = aiqa_secret_config_validate(&snapshot->secrets);
    }
    aiqa_config_record_secure_clear(&active);
    return ESP_OK;
}

esp_err_t aiqa_config_load_from_nvs(aiqa_config_snapshot_t *snapshot)
{
    if (!aiqa_config_lifecycle_try_lock()) {
        if (snapshot != NULL) {
            aiqa_config_snapshot_secure_clear(snapshot);
        }
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t ret = load_snapshot_unlocked(snapshot);
    aiqa_config_lifecycle_unlock();
    return ret;
}

esp_err_t aiqa_config_nvs_verify_record(
    aiqa_config_slot_t slot,
    const aiqa_config_record_t *expected)
{
    if (slot_namespace(slot) == NULL || expected == NULL || expected->active_slot != slot) {
        return ESP_ERR_INVALID_ARG;
    }
    aiqa_config_record_t actual = {0};
    esp_err_t ret = load_slot_record(slot, &actual);
    if (ret == ESP_OK && !records_equal(&actual, expected)) {
        ret = ESP_ERR_INVALID_STATE;
    }
    aiqa_config_record_secure_clear(&actual);
    return ret;
}

esp_err_t aiqa_config_nvs_stage_record(
    aiqa_config_slot_t slot,
    const aiqa_config_record_t *record)
{
    const char *namespace_name = slot_namespace(slot);
    if (namespace_name == NULL || record == NULL || record->active_slot != slot ||
        record->revision == 0 || aiqa_config_validate(&record->config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(&record->secrets) != AIQA_SECRET_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!try_lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t current_head = 0;
    esp_err_t head_ret = read_head(&current_head);
    if (head_ret == ESP_OK) {
        aiqa_config_slot_t active_slot = AIQA_CONFIG_SLOT_LEGACY;
        uint32_t active_revision = 0;
        if (!decode_head(current_head, &active_slot, &active_revision)) {
            unlock_storage();
            return ESP_ERR_INVALID_STATE;
        }
        if (active_slot == slot) {
            unlock_storage();
            return ESP_ERR_INVALID_STATE;
        }
    } else if (head_ret != ESP_ERR_NVS_NOT_FOUND) {
        unlock_storage();
        return head_ret;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_erase_all(handle);
    }
    if (ret == ESP_OK) {
        ret = write_record_fields(handle, record);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    aiqa_config_record_t actual = {0};
    esp_err_t verify_ret = load_slot_record(slot, &actual);
    if (verify_ret == ESP_OK && !records_equal(&actual, record)) {
        verify_ret = ESP_ERR_INVALID_STATE;
    }
    aiqa_config_record_secure_clear(&actual);
    unlock_storage();
    if (verify_ret == ESP_OK) {
        return ESP_OK;
    }
    return ret != ESP_OK ? ret : verify_ret;
}

aiqa_config_activation_result_t aiqa_config_nvs_activate_record(
    const aiqa_config_record_t *candidate,
    aiqa_config_slot_t expected_slot,
    uint32_t expected_revision)
{
    if (candidate == NULL || slot_namespace(candidate->active_slot) == NULL ||
        candidate->revision == 0 || expected_revision == 0 ||
        (expected_slot != AIQA_CONFIG_SLOT_LEGACY && slot_namespace(expected_slot) == NULL)) {
        return AIQA_CONFIG_ACTIVATION_INDETERMINATE;
    }
    const bool valid_successor =
        (expected_slot == AIQA_CONFIG_SLOT_LEGACY &&
         candidate->active_slot == AIQA_CONFIG_SLOT_A &&
         candidate->revision == 1 && expected_revision == 1) ||
        (expected_slot != AIQA_CONFIG_SLOT_LEGACY &&
         candidate->active_slot != expected_slot &&
         expected_revision != UINT32_MAX &&
         candidate->revision == expected_revision + 1U);
    if (!valid_successor || !try_lock_storage()) {
        return AIQA_CONFIG_ACTIVATION_NOT_COMMITTED;
    }
    aiqa_config_record_t target_record = {0};
    const esp_err_t target_ret = load_slot_record(candidate->active_slot, &target_record);
    const bool target_valid = target_ret == ESP_OK && records_equal(&target_record, candidate);
    aiqa_config_record_secure_clear(&target_record);
    if (!target_valid) {
        unlock_storage();
        return AIQA_CONFIG_ACTIVATION_NOT_COMMITTED;
    }
    const uint64_t target = encode_head(candidate->active_slot, candidate->revision);
    uint64_t previous = 0;
    const esp_err_t previous_ret = read_head(&previous);
    if (previous_ret != ESP_OK && previous_ret != ESP_ERR_NVS_NOT_FOUND) {
        unlock_storage();
        return AIQA_CONFIG_ACTIVATION_INDETERMINATE;
    }
    const bool expected_head_matches =
        (expected_slot == AIQA_CONFIG_SLOT_LEGACY && previous_ret == ESP_ERR_NVS_NOT_FOUND) ||
        (expected_slot != AIQA_CONFIG_SLOT_LEGACY && previous_ret == ESP_OK &&
         previous == encode_head(expected_slot, expected_revision));
    if (!expected_head_matches) {
        unlock_storage();
        return AIQA_CONFIG_ACTIVATION_NOT_COMMITTED;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE_META, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_u64(handle, AIQA_NVS_KEY_HEAD, target);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    uint64_t durable = 0;
    const esp_err_t durable_ret = read_head(&durable);
    if (durable_ret == ESP_OK && durable == target) {
        unlock_storage();
        return AIQA_CONFIG_ACTIVATION_COMMITTED;
    }
    if ((previous_ret == ESP_ERR_NVS_NOT_FOUND && durable_ret == ESP_ERR_NVS_NOT_FOUND) ||
        (previous_ret == ESP_OK && durable_ret == ESP_OK && durable == previous)) {
        unlock_storage();
        return AIQA_CONFIG_ACTIVATION_NOT_COMMITTED;
    }
    unlock_storage();
    return AIQA_CONFIG_ACTIVATION_INDETERMINATE;
}

bool aiqa_config_nvs_discard_record(aiqa_config_slot_t slot)
{
    const char *namespace_name = slot == AIQA_CONFIG_SLOT_LEGACY
                                     ? AIQA_NVS_NAMESPACE_LEGACY
                                     : slot_namespace(slot);
    if (namespace_name == NULL) {
        return false;
    }
    if (!try_lock_storage()) {
        return false;
    }
    uint64_t current_head = 0;
    esp_err_t head_ret = read_head(&current_head);
    if (head_ret == ESP_OK) {
        aiqa_config_slot_t active_slot = AIQA_CONFIG_SLOT_LEGACY;
        uint32_t active_revision = 0;
        if (!decode_head(current_head, &active_slot, &active_revision) ||
            active_slot == slot) {
            unlock_storage();
            return false;
        }
    } else if (head_ret != ESP_ERR_NVS_NOT_FOUND ||
               slot == AIQA_CONFIG_SLOT_LEGACY) {
        unlock_storage();
        return false;
    }
    if (slot == AIQA_CONFIG_SLOT_LEGACY) {
        const bool discarded =
            aiqa_config_erase_legacy_record_from_nvs() == ESP_OK;
        unlock_storage();
        return discarded;
    }
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_erase_all(handle);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (ret != ESP_OK) {
        unlock_storage();
        return false;
    }

    ret = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        unlock_storage();
        return true;
    }
    if (ret != ESP_OK) {
        unlock_storage();
        return false;
    }
    uint32_t layout = 0;
    ret = nvs_get_u32(handle, AIQA_NVS_KEY_LAYOUT, &layout);
    nvs_close(handle);
    const bool discarded = ret == ESP_ERR_NVS_NOT_FOUND;
    unlock_storage();
    return discarded;
}

static bool storage_stage(
    void *context,
    aiqa_config_slot_t slot,
    const aiqa_config_record_t *candidate)
{
    (void)context;
    return aiqa_config_nvs_stage_record(slot, candidate) == ESP_OK;
}

static bool storage_verify(
    void *context,
    aiqa_config_slot_t slot,
    const aiqa_config_record_t *candidate)
{
    (void)context;
    return aiqa_config_nvs_verify_record(slot, candidate) == ESP_OK;
}

static aiqa_config_activation_result_t storage_activate(
    void *context,
    const aiqa_config_record_t *candidate,
    aiqa_config_slot_t expected_slot,
    uint32_t expected_revision)
{
    (void)context;
    return aiqa_config_nvs_activate_record(candidate, expected_slot, expected_revision);
}

static bool storage_discard(void *context, aiqa_config_slot_t slot)
{
    (void)context;
    return aiqa_config_nvs_discard_record(slot);
}

aiqa_config_storage_ports_t aiqa_config_nvs_storage_ports(void)
{
    aiqa_config_storage_ports_t ports = {
        .context = NULL,
        .stage = storage_stage,
        .verify = storage_verify,
        .activate = storage_activate,
        .discard = storage_discard,
    };
    return ports;
}

static esp_err_t erase_namespace(const char *namespace_name)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_erase_all(handle);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return ret;
}

static esp_err_t complete_pending_reset_locked(void)
{
    esp_err_t ret = erase_namespace(AIQA_NVS_NAMESPACE_LEGACY);
    if (ret == ESP_OK) {
        ret = erase_namespace(AIQA_NVS_NAMESPACE_SLOT_A);
    }
    if (ret == ESP_OK) {
        ret = erase_namespace(AIQA_NVS_NAMESPACE_SLOT_B);
    }
    if (ret == ESP_OK) {
        ret = erase_namespace(AIQA_NVS_NAMESPACE_META);
    }
    return ret;
}

static esp_err_t complete_pending_reset(void)
{
    if (!try_lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t ret = complete_pending_reset_locked();
    unlock_storage();
    return ret;
}

static esp_err_t write_reset_tombstone(void)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(AIQA_NVS_NAMESPACE_META, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, AIQA_NVS_KEY_RESET, 1U);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (ret != ESP_OK) {
        return ret;
    }
    bool pending = false;
    ret = reset_is_pending(&pending);
    return ret == ESP_OK && pending ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t aiqa_config_erase_nvs_namespace(void)
{
    if (!aiqa_config_lifecycle_try_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!try_lock_storage()) {
        aiqa_config_lifecycle_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = write_reset_tombstone();
    if (ret == ESP_OK) {
        ret = complete_pending_reset_locked();
    }
    unlock_storage();
    aiqa_config_lifecycle_unlock();
    return ret;
}
