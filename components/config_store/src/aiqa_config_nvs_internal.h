#pragma once

#include "aiqa_config_nvs.h"

esp_err_t aiqa_config_load_legacy_from_nvs(aiqa_config_snapshot_t *snapshot);
esp_err_t aiqa_config_load_legacy_prefs_from_nvs(aiqa_user_prefs_t *prefs);
esp_err_t aiqa_config_erase_legacy_nvs_namespace(void);
