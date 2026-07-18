#include "aiqa_pairing_esp_platform.h"

#include "bootloader_random.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform_util.h"

#include <stdatomic.h>
#include <string.h>

static const unsigned char PERSONALIZATION[] = "aiqa-management-pairing-v1";
static mbedtls_ctr_drbg_context s_drbg;
static atomic_bool s_entropy_ready = ATOMIC_VAR_INIT(false);
static atomic_uint_least32_t s_local_actions = ATOMIC_VAR_INIT(0);
static uint8_t s_device_id[AIQA_PAIRING_DEVICE_ID_MAX];

static int boot_entropy(void *context, unsigned char *output, size_t output_length)
{
    (void)context;
    if (output == NULL || output_length == 0U) {
        return -1;
    }
    esp_fill_random(output, output_length);
    return 0;
}

esp_err_t aiqa_pairing_esp_entropy_init(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &s_entropy_ready,
            &expected,
            false,
            memory_order_acq_rel,
            memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    mbedtls_platform_zeroize(s_device_id, sizeof(s_device_id));
    mbedtls_ctr_drbg_init(&s_drbg);
    bootloader_random_enable();
    const int seed_result = mbedtls_ctr_drbg_seed(
        &s_drbg,
        boot_entropy,
        NULL,
        PERSONALIZATION,
        sizeof(PERSONALIZATION) - 1U);
    int identity_result = -1;
    if (seed_result == 0) {
        identity_result = mbedtls_ctr_drbg_random(
            &s_drbg, s_device_id, sizeof(s_device_id));
    }
    bootloader_random_disable();
    if (seed_result != 0 || identity_result != 0) {
        mbedtls_ctr_drbg_free(&s_drbg);
        mbedtls_platform_zeroize(s_device_id, sizeof(s_device_id));
        return ESP_FAIL;
    }
    atomic_store_explicit(&s_entropy_ready, true, memory_order_release);
    return ESP_OK;
}

int aiqa_pairing_esp_random_fill(
    void *context,
    unsigned char *output,
    size_t output_length)
{
    (void)context;
    if (!atomic_load_explicit(&s_entropy_ready, memory_order_acquire) ||
        output == NULL || output_length == 0U) {
        return -1;
    }
    return mbedtls_ctr_drbg_random(&s_drbg, output, output_length);
}

bool aiqa_pairing_esp_monotonic_ms(void *context, uint64_t *out_now_ms)
{
    (void)context;
    if (out_now_ms == NULL) {
        return false;
    }
    const int64_t microseconds = esp_timer_get_time();
    if (microseconds < 0) {
        return false;
    }
    *out_now_ms = (uint64_t)microseconds / UINT64_C(1000);
    return true;
}

bool aiqa_pairing_esp_copy_device_id(
    uint8_t output[AIQA_PAIRING_DEVICE_ID_MAX])
{
    if (output == NULL ||
        !atomic_load_explicit(&s_entropy_ready, memory_order_acquire)) {
        return false;
    }
    (void)memcpy(output, s_device_id, sizeof(s_device_id));
    return true;
}

static uint32_t action_bit(aiqa_pairing_local_action_t action)
{
    return action == AIQA_PAIRING_LOCAL_START
               ? UINT32_C(1)
               : action == AIQA_PAIRING_LOCAL_RESET ? UINT32_C(2) : 0U;
}

bool aiqa_pairing_esp_post_local_action(aiqa_pairing_local_action_t action)
{
    const uint32_t bit = action_bit(action);
    if (bit == 0U) {
        return false;
    }
    (void)atomic_fetch_or_explicit(&s_local_actions, bit, memory_order_release);
    return true;
}

aiqa_pairing_local_action_t aiqa_pairing_esp_pending_local_action(void)
{
    const uint32_t pending =
        atomic_load_explicit(&s_local_actions, memory_order_acquire);
    if ((pending & UINT32_C(2)) != 0U) {
        return AIQA_PAIRING_LOCAL_RESET;
    }
    return (pending & UINT32_C(1)) != 0U ? AIQA_PAIRING_LOCAL_START : 0;
}

bool aiqa_pairing_esp_consume_local_action(
    void *context,
    aiqa_pairing_local_action_t action)
{
    (void)context;
    const uint32_t bit = action_bit(action);
    if (bit == 0U) {
        return false;
    }
    uint32_t current = atomic_load_explicit(&s_local_actions, memory_order_acquire);
    while ((current & bit) != 0U) {
        if (atomic_compare_exchange_weak_explicit(
                &s_local_actions,
                &current,
                current & ~bit,
                memory_order_acq_rel,
                memory_order_acquire)) {
            return true;
        }
    }
    return false;
}
