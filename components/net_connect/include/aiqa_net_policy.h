#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_NET_MIN_VALID_UNIX_TIME 1704067200LL
#define AIQA_NET_DEFAULT_TIMEZONE "CST-8"

typedef struct {
    uint32_t connect_timeout_ms;
    uint32_t sntp_timeout_ms;
    uint8_t max_wifi_retries;
    const char *sntp_server;
} aiqa_net_policy_t;

aiqa_net_policy_t aiqa_net_default_policy(void);
bool aiqa_net_time_is_valid(time_t unix_seconds);
uint32_t aiqa_net_retry_delay_ms(uint8_t attempt);

#ifdef __cplusplus
}
#endif
