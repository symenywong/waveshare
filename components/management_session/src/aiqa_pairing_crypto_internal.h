#pragma once

#include "aiqa_pairing_crypto.h"

#include <stdint.h>

typedef struct {
  uint8_t client_to_device_key[AIQA_PAIRING_KEY_SIZE];
  uint8_t device_to_client_key[AIQA_PAIRING_KEY_SIZE];
  uint8_t client_to_device_nonce_prefix[AIQA_PAIRING_NONCE_PREFIX_SIZE];
  uint8_t device_to_client_nonce_prefix[AIQA_PAIRING_NONCE_PREFIX_SIZE];
  uint8_t client_finished_key[AIQA_PAIRING_KEY_SIZE];
  uint8_t device_finished_key[AIQA_PAIRING_KEY_SIZE];
  uint8_t session_id[AIQA_PAIRING_SESSION_ID_SIZE];
  uint8_t transcript_hash[AIQA_PAIRING_KEY_SIZE];
} aiqa_pairing_key_material_t;

aiqa_pairing_result_t
aiqa_pairing_consume_confirmed_keys(aiqa_pairing_keys_t **keys,
                                    aiqa_pairing_role_t *out_local_role,
                                    aiqa_pairing_key_material_t *out_material);
