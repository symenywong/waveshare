#pragma once

#include "aiqa_config.h"
#include "aiqa_assistant_profile.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t volume_percent;
    aiqa_assistant_profile_t assistant_profile;
} aiqa_user_prefs_t;

typedef struct {
    aiqa_config_t config;
    aiqa_secret_config_t secrets;
    aiqa_user_prefs_t user_prefs;
    aiqa_config_status_t config_status;
    aiqa_secret_status_t secret_status;
    bool namespace_found;
} aiqa_config_snapshot_t;

esp_err_t aiqa_config_load_from_nvs(aiqa_config_snapshot_t *snapshot);
esp_err_t aiqa_config_erase_nvs_namespace(void);
esp_err_t aiqa_config_save_volume_percent(uint8_t volume_percent);
esp_err_t aiqa_config_save_assistant_profile(const aiqa_assistant_profile_t *profile);

#ifdef __cplusplus
}
#endif
