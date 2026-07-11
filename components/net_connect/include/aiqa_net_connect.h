#pragma once

#include "aiqa_config.h"
#include "aiqa_net_policy.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t aiqa_net_connect_wifi_and_sync_time(
    const aiqa_secret_config_t *secrets,
    const aiqa_net_policy_t *policy);

#ifdef __cplusplus
}
#endif
