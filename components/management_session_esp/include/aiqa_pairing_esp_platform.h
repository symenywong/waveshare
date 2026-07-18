#pragma once

#include "aiqa_pairing_lifecycle.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Must run before runtime RF, ADC, or I2S initialization. */
esp_err_t aiqa_pairing_esp_entropy_init(void);
int aiqa_pairing_esp_random_fill(
    void *context,
    unsigned char *output,
    size_t output_length);
bool aiqa_pairing_esp_monotonic_ms(void *context, uint64_t *out_now_ms);
bool aiqa_pairing_esp_copy_device_id(
    uint8_t output[AIQA_PAIRING_DEVICE_ID_MAX]);

bool aiqa_pairing_esp_post_local_action(aiqa_pairing_local_action_t action);
aiqa_pairing_local_action_t aiqa_pairing_esp_pending_local_action(void);
bool aiqa_pairing_esp_consume_local_action(
    void *context,
    aiqa_pairing_local_action_t action);

#ifdef __cplusplus
}
#endif
