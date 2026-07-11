#include "aiqa_net_policy.h"

aiqa_net_policy_t aiqa_net_default_policy(void)
{
    return (aiqa_net_policy_t){
        .connect_timeout_ms = 30000,
        .sntp_timeout_ms = 15000,
        .max_wifi_retries = 5,
        .sntp_server = "pool.ntp.org",
    };
}

bool aiqa_net_time_is_valid(time_t unix_seconds)
{
    return unix_seconds >= (time_t)AIQA_NET_MIN_VALID_UNIX_TIME;
}

uint32_t aiqa_net_retry_delay_ms(uint8_t attempt)
{
    uint32_t delay = 500;
    for (uint8_t index = 0; index < attempt && delay < 5000; ++index) {
        delay *= 2;
        if (delay > 5000) {
            delay = 5000;
        }
    }
    return delay;
}
