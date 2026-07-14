#include "aiqa_pairing_esp_nvs.h"

#include "fake_nvs.h"
#include "nvs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define PAIRING_NAMESPACE "aiqa_pair"
#define PAIRING_LOCK_KEY "lock"

static aiqa_pairing_lock_record_t record(uint32_t attempts_used) {
  return (aiqa_pairing_lock_record_t){
      .version = AIQA_PAIRING_LOCK_RECORD_VERSION,
      .attempts_used = attempts_used,
  };
}

static aiqa_pairing_lock_record_t load_ok(void) {
  aiqa_pairing_lock_record_t loaded = {0};
  assert(aiqa_pairing_esp_nvs_load_lock_record(NULL, &loaded) ==
         AIQA_PAIRING_PERSISTENCE_OK);
  return loaded;
}

static void write_raw_u64(uint64_t value) {
  nvs_handle_t handle = 0;
  assert(nvs_open(PAIRING_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK);
  assert(nvs_set_u64(handle, PAIRING_LOCK_KEY, value) == ESP_OK);
  assert(nvs_commit(handle) == ESP_OK);
  nvs_close(handle);
}

static void run_missing_and_roundtrip(void) {
  fake_nvs_reset();
  aiqa_pairing_lock_record_t loaded = {0};
  assert(aiqa_pairing_esp_nvs_load_lock_record(NULL, &loaded) ==
         AIQA_PAIRING_PERSISTENCE_NOT_FOUND);

  const aiqa_pairing_lock_record_t initial = record(0);
  assert(aiqa_pairing_esp_nvs_store_lock_record(NULL, &initial));
  fake_nvs_power_cut();
  loaded = load_ok();
  assert(loaded.version == AIQA_PAIRING_LOCK_RECORD_VERSION);
  assert(loaded.attempts_used == 0);

  const aiqa_pairing_lock_record_t updated = record(2);
  assert(aiqa_pairing_esp_nvs_store_lock_record(NULL, &updated));
  fake_nvs_power_cut();
  loaded = load_ok();
  assert(loaded.attempts_used == 2);
}

static void run_commit_reconciliation(void) {
  fake_nvs_reset();
  const aiqa_pairing_lock_record_t two = record(2);
  const aiqa_pairing_lock_record_t three = record(3);
  const aiqa_pairing_lock_record_t four = record(4);
  assert(aiqa_pairing_esp_nvs_store_lock_record(NULL, &two));

  fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_NOT_APPLIED_ERROR);
  assert(!aiqa_pairing_esp_nvs_store_lock_record(NULL, &three));
  fake_nvs_power_cut();
  assert(load_ok().attempts_used == 2);

  fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_APPLIED_ERROR);
  assert(!aiqa_pairing_esp_nvs_store_lock_record(NULL, &three));
  fake_nvs_power_cut();
  assert(load_ok().attempts_used == 3);

  fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_APPLIED_ERROR_READBACK_FAIL);
  assert(!aiqa_pairing_esp_nvs_store_lock_record(NULL, &four));
  fake_nvs_power_cut();
  aiqa_pairing_lock_record_t unreadable = {0};
  assert(aiqa_pairing_esp_nvs_load_lock_record(NULL, &unreadable) ==
         AIQA_PAIRING_PERSISTENCE_ERROR);
  assert(load_ok().attempts_used == 4);
}

static void run_corrupt_and_invalid(void) {
  fake_nvs_reset();
  assert(aiqa_pairing_esp_nvs_load_lock_record(NULL, NULL) ==
         AIQA_PAIRING_PERSISTENCE_ERROR);
  assert(!aiqa_pairing_esp_nvs_store_lock_record(NULL, NULL));

  aiqa_pairing_lock_record_t invalid = record(0);
  invalid.version += 1U;
  assert(!aiqa_pairing_esp_nvs_store_lock_record(NULL, &invalid));
  invalid = record(AIQA_PAIRING_MAX_ATTEMPTS + 1U);
  assert(!aiqa_pairing_esp_nvs_store_lock_record(NULL, &invalid));

  write_raw_u64((UINT64_C(2) << 32) | UINT64_C(1));
  aiqa_pairing_lock_record_t loaded = {0};
  assert(aiqa_pairing_esp_nvs_load_lock_record(NULL, &loaded) ==
         AIQA_PAIRING_PERSISTENCE_ERROR);

  fake_nvs_reset();
  write_raw_u64((UINT64_C(1) << 32) |
                (uint64_t)(AIQA_PAIRING_MAX_ATTEMPTS + 1U));
  assert(aiqa_pairing_esp_nvs_load_lock_record(NULL, &loaded) ==
         AIQA_PAIRING_PERSISTENCE_ERROR);

  fake_nvs_reset();
  nvs_handle_t handle = 0;
  assert(nvs_open(PAIRING_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK);
  assert(nvs_set_str(handle, PAIRING_LOCK_KEY, "not-a-lock-record") == ESP_OK);
  assert(nvs_commit(handle) == ESP_OK);
  nvs_close(handle);
  assert(aiqa_pairing_esp_nvs_load_lock_record(NULL, &loaded) ==
         AIQA_PAIRING_PERSISTENCE_ERROR);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  if (strcmp(argv[1], "missing-roundtrip") == 0)
    run_missing_and_roundtrip();
  else if (strcmp(argv[1], "commit-reconciliation") == 0)
    run_commit_reconciliation();
  else if (strcmp(argv[1], "corrupt-invalid") == 0)
    run_corrupt_and_invalid();
  else
    return 2;
  return 0;
}
