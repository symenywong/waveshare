#pragma once

#include "aiqa_config.h"

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    aiqa_config_t config;
    aiqa_secret_config_t secrets;
    aiqa_config_status_t config_status;
    aiqa_secret_status_t secret_status;
    bool namespace_found;
} aiqa_config_snapshot_t;

esp_err_t aiqa_config_load_from_nvs(aiqa_config_snapshot_t *snapshot);
esp_err_t aiqa_config_erase_nvs_namespace(void);

#ifdef __cplusplus
}
#endif
