#pragma once

#include "aiqa_pairing_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ESP-IDF NVS adapter for the lifecycle persistence port. The context is
 * reserved for future dependency injection and is currently ignored.
 */
aiqa_pairing_persistence_result_t
aiqa_pairing_esp_nvs_load_lock_record(void *context,
                                      aiqa_pairing_lock_record_t *out_record);
bool aiqa_pairing_esp_nvs_store_lock_record(
    void *context, const aiqa_pairing_lock_record_t *record);

#ifdef __cplusplus
}
#endif
