#include "aiqa_pairing_esp_nvs.h"

#include "nvs.h"

#include <stdint.h>

#define AIQA_PAIRING_NVS_NAMESPACE "aiqa_pair"
#define AIQA_PAIRING_NVS_LOCK_KEY "lock"

static uint64_t encode_record(const aiqa_pairing_lock_record_t *record) {
  return ((uint64_t)record->version << 32) | (uint64_t)record->attempts_used;
}

static bool decode_record(uint64_t encoded,
                          aiqa_pairing_lock_record_t *out_record) {
  const aiqa_pairing_lock_record_t decoded = {
      .version = (uint32_t)(encoded >> 32),
      .attempts_used = (uint32_t)encoded,
  };
  if (decoded.version != AIQA_PAIRING_LOCK_RECORD_VERSION ||
      decoded.attempts_used > AIQA_PAIRING_MAX_ATTEMPTS)
    return false;
  *out_record = decoded;
  return true;
}

aiqa_pairing_persistence_result_t
aiqa_pairing_esp_nvs_load_lock_record(void *context,
                                      aiqa_pairing_lock_record_t *out_record) {
  (void)context;
  if (out_record == NULL)
    return AIQA_PAIRING_PERSISTENCE_ERROR;

  nvs_handle_t handle = 0;
  const esp_err_t open_result =
      nvs_open(AIQA_PAIRING_NVS_NAMESPACE, NVS_READONLY, &handle);
  if (open_result == ESP_ERR_NVS_NOT_FOUND)
    return AIQA_PAIRING_PERSISTENCE_NOT_FOUND;
  if (open_result != ESP_OK)
    return AIQA_PAIRING_PERSISTENCE_ERROR;

  uint64_t encoded = 0;
  const esp_err_t read_result =
      nvs_get_u64(handle, AIQA_PAIRING_NVS_LOCK_KEY, &encoded);
  nvs_close(handle);
  if (read_result != ESP_OK || !decode_record(encoded, out_record))
    return AIQA_PAIRING_PERSISTENCE_ERROR;
  return AIQA_PAIRING_PERSISTENCE_OK;
}

bool aiqa_pairing_esp_nvs_store_lock_record(
    void *context, const aiqa_pairing_lock_record_t *record) {
  (void)context;
  if (record == NULL || record->version != AIQA_PAIRING_LOCK_RECORD_VERSION ||
      record->attempts_used > AIQA_PAIRING_MAX_ATTEMPTS)
    return false;

  const uint64_t target = encode_record(record);
  nvs_handle_t handle = 0;
  if (nvs_open(AIQA_PAIRING_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return false;
  const esp_err_t set_result =
      nvs_set_u64(handle, AIQA_PAIRING_NVS_LOCK_KEY, target);
  const esp_err_t commit_result =
      set_result == ESP_OK ? nvs_commit(handle) : ESP_FAIL;
  nvs_close(handle);
  if (set_result != ESP_OK || commit_result != ESP_OK)
    return false;

  aiqa_pairing_lock_record_t durable = {0};
  const aiqa_pairing_persistence_result_t readback_result =
      aiqa_pairing_esp_nvs_load_lock_record(context, &durable);
  const bool exact_match = readback_result == AIQA_PAIRING_PERSISTENCE_OK &&
                           encode_record(&durable) == target;
  return exact_match;
}
