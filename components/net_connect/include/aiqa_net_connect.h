#pragma once

#include "aiqa_config.h"
#include "aiqa_config_transaction.h"
#include "aiqa_net_policy.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t aiqa_net_connect_wifi(
    const aiqa_wifi_credentials_t *credentials,
    const aiqa_net_policy_t *policy);

esp_err_t aiqa_net_sync_time(const aiqa_net_policy_t *policy);
bool aiqa_net_time_is_synchronized(void);
esp_err_t aiqa_net_disconnect_wifi(void);
/* Network-owner-only reset path; deinitializes the driver to discard RAM credentials. */
esp_err_t aiqa_net_forget_wifi(void);

typedef struct {
    aiqa_net_policy_t policy;
} aiqa_net_transaction_adapter_t;

void aiqa_net_transaction_adapter_init(
    aiqa_net_transaction_adapter_t *adapter,
    const aiqa_net_policy_t *policy);

/* Transaction ports invoke Wi-Fi driver APIs and must run on the network-owner task. */
aiqa_config_network_ports_t aiqa_net_transaction_ports(
    aiqa_net_transaction_adapter_t *adapter);

esp_err_t aiqa_net_connect_wifi_and_sync_time(
    const aiqa_wifi_credentials_t *credentials,
    const aiqa_net_policy_t *policy);

#ifdef __cplusplus
}
#endif
