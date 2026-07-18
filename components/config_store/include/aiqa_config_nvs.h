#pragma once

#include "aiqa_config.h"
#include "aiqa_assistant_profile.h"
#include "aiqa_language.h"
#include "aiqa_config_transaction.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t volume_percent;
    aiqa_assistant_profile_t assistant_profile;
    aiqa_dialogue_language_t dialogue_language;
} aiqa_user_prefs_t;

typedef struct {
    aiqa_config_t config;
    aiqa_secret_config_t secrets;
    aiqa_user_prefs_t user_prefs;
    aiqa_config_status_t config_status;
    aiqa_secret_status_t secret_status;
    uint32_t revision;
    aiqa_config_slot_t active_slot;
    bool namespace_found;
} aiqa_config_snapshot_t;

esp_err_t aiqa_config_load_from_nvs(aiqa_config_snapshot_t *snapshot);
void aiqa_config_snapshot_secure_clear(aiqa_config_snapshot_t *snapshot);
esp_err_t aiqa_config_nvs_load_active_record(aiqa_config_record_t *record, bool *found);
esp_err_t aiqa_config_nvs_stage_record(
    aiqa_config_slot_t slot,
    const aiqa_config_record_t *record);
esp_err_t aiqa_config_nvs_verify_record(
    aiqa_config_slot_t slot,
    const aiqa_config_record_t *expected);
aiqa_config_activation_result_t aiqa_config_nvs_activate_record(
    const aiqa_config_record_t *candidate,
    aiqa_config_slot_t expected_slot,
    uint32_t expected_revision);
bool aiqa_config_nvs_discard_record(aiqa_config_slot_t slot);
aiqa_config_storage_ports_t aiqa_config_nvs_storage_ports(void);
esp_err_t aiqa_config_erase_nvs_namespace(void);
esp_err_t aiqa_config_save_volume_percent(uint8_t volume_percent);
esp_err_t aiqa_config_save_assistant_profile(const aiqa_assistant_profile_t *profile);
esp_err_t aiqa_config_save_dialogue_language(aiqa_dialogue_language_t language);

#ifdef __cplusplus
}
#endif
