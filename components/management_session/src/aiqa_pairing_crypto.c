#include "aiqa_pairing_crypto.h"
#include "aiqa_pairing_crypto_internal.h"

#include "mbedtls/constant_time.h"
#include "mbedtls/ecjpake.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define AIQA_PAIRING_SHARED_POINT_MAX 128U

static const uint8_t TRANSCRIPT_DOMAIN[] = "AIQA-MGMT-ECJPAKE-v1";
static const uint8_t TRANSPORT_NAME[] = "usb-serial-jtag";
static const uint8_t SALT_LABEL[] = "aiqa-management/v1 hkdf salt";

struct aiqa_pairing_context {
  mbedtls_ecjpake_context pake;
  aiqa_pairing_role_t role;
  uint32_t credential_id;
  uint64_t handshake_id;
  uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX];
  size_t device_id_length;
  uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE];
  uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE];
  uint8_t client_round_one[AIQA_PAIRING_ROUND_MAX];
  uint8_t device_round_one[AIQA_PAIRING_ROUND_MAX];
  uint8_t client_round_two[AIQA_PAIRING_ROUND_MAX];
  uint8_t device_round_two[AIQA_PAIRING_ROUND_MAX];
  size_t client_round_one_length;
  size_t device_round_one_length;
  size_t client_round_two_length;
  size_t device_round_two_length;
  aiqa_pairing_random_fn random;
  void *random_context;
  bool own_round_one_written;
  bool peer_round_one_read;
  bool own_round_two_written;
  bool peer_round_two_read;
  bool derived;
  bool failed;
  bool pake_active;
};

struct aiqa_pairing_keys {
  aiqa_pairing_key_material_t material;
  aiqa_pairing_role_t local_role;
  bool local_finished_created;
  bool peer_finished_verified;
  bool failed;
};

static bool role_is_valid(aiqa_pairing_role_t role) {
  return role == AIQA_PAIRING_CLIENT || role == AIQA_PAIRING_DEVICE;
}

static bool pairing_code_is_valid(const uint8_t *code, size_t length) {
  if (code == NULL || length != AIQA_PAIRING_CODE_SIZE) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    if (code[index] < '0' || code[index] > '9') {
      return false;
    }
  }
  return true;
}

static void store_round_for_role(aiqa_pairing_context_t *context,
                                 aiqa_pairing_role_t role, unsigned round,
                                 const uint8_t *message,
                                 size_t message_length) {
  uint8_t *destination = NULL;
  size_t *destination_length = NULL;
  if (role == AIQA_PAIRING_CLIENT) {
    destination =
        round == 1U ? context->client_round_one : context->client_round_two;
    destination_length = round == 1U ? &context->client_round_one_length
                                     : &context->client_round_two_length;
  } else {
    destination =
        round == 1U ? context->device_round_one : context->device_round_two;
    destination_length = round == 1U ? &context->device_round_one_length
                                     : &context->device_round_two_length;
  }
  (void)memcpy(destination, message, message_length);
  *destination_length = message_length;
}

static void fail_context(aiqa_pairing_context_t *context) {
  context->failed = true;
  if (context->pake_active) {
    mbedtls_ecjpake_free(&context->pake);
    context->pake_active = false;
  }
}

static void write_u16_be(uint8_t output[2], size_t value) {
  output[0] = (uint8_t)(value >> 8U);
  output[1] = (uint8_t)value;
}

static void write_u32_be(uint8_t output[4], uint32_t value) {
  output[0] = (uint8_t)(value >> 24U);
  output[1] = (uint8_t)(value >> 16U);
  output[2] = (uint8_t)(value >> 8U);
  output[3] = (uint8_t)value;
}

static void write_u64_be(uint8_t output[8], uint64_t value) {
  for (size_t index = 0; index < 8; ++index) {
    output[7U - index] = (uint8_t)(value >> (index * 8U));
  }
}

static int hash_field(mbedtls_sha256_context *hash, const uint8_t *value,
                      size_t value_length) {
  uint8_t length[2];
  if (value_length > UINT16_MAX) {
    return -1;
  }
  write_u16_be(length, value_length);
  int result = mbedtls_sha256_update(hash, length, sizeof(length));
  if (result == 0 && value_length > 0) {
    result = mbedtls_sha256_update(hash, value, value_length);
  }
  return result;
}

static int transcript_hash(const aiqa_pairing_context_t *context,
                           uint8_t output[AIQA_PAIRING_KEY_SIZE]) {
  uint8_t metadata[16] = {1, 1, AIQA_PAIRING_CLIENT, AIQA_PAIRING_DEVICE};
  write_u32_be(&metadata[4], context->credential_id);
  write_u64_be(&metadata[8], context->handshake_id);
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  int result = mbedtls_sha256_starts(&hash, 0);
  if (result == 0)
    result =
        hash_field(&hash, TRANSCRIPT_DOMAIN, sizeof(TRANSCRIPT_DOMAIN) - 1U);
  if (result == 0)
    result = mbedtls_sha256_update(&hash, metadata, sizeof(metadata));
  if (result == 0)
    result = hash_field(&hash, context->device_id, context->device_id_length);
  if (result == 0)
    result = hash_field(&hash, TRANSPORT_NAME, sizeof(TRANSPORT_NAME) - 1U);
  if (result == 0)
    result = mbedtls_sha256_update(&hash, context->client_nonce,
                                   sizeof(context->client_nonce));
  if (result == 0)
    result = mbedtls_sha256_update(&hash, context->device_nonce,
                                   sizeof(context->device_nonce));
  if (result == 0)
    result = hash_field(&hash, context->client_round_one,
                        context->client_round_one_length);
  if (result == 0)
    result = hash_field(&hash, context->device_round_one,
                        context->device_round_one_length);
  if (result == 0)
    result = hash_field(&hash, context->client_round_two,
                        context->client_round_two_length);
  if (result == 0)
    result = hash_field(&hash, context->device_round_two,
                        context->device_round_two_length);
  if (result == 0)
    result = mbedtls_sha256_finish(&hash, output);
  mbedtls_sha256_free(&hash);
  return result;
}

static int expand_key(const mbedtls_md_info_t *md,
                      const uint8_t prk[AIQA_PAIRING_KEY_SIZE],
                      const char *label,
                      const uint8_t transcript[AIQA_PAIRING_KEY_SIZE],
                      uint8_t *output, size_t output_length) {
  uint8_t info[96];
  const size_t label_length = strlen(label);
  if (label_length + 1U + AIQA_PAIRING_KEY_SIZE > sizeof(info)) {
    return -1;
  }
  (void)memcpy(info, label, label_length);
  info[label_length] = 0;
  (void)memcpy(info + label_length + 1U, transcript, AIQA_PAIRING_KEY_SIZE);
  const int result = mbedtls_hkdf_expand(
      md, prk, AIQA_PAIRING_KEY_SIZE, info,
      label_length + 1U + AIQA_PAIRING_KEY_SIZE, output, output_length);
  mbedtls_platform_zeroize(info, sizeof(info));
  return result;
}

aiqa_pairing_result_t
aiqa_pairing_create(aiqa_pairing_context_t **out_context,
                    aiqa_pairing_role_t role, const uint8_t *pairing_code,
                    size_t pairing_code_length, uint32_t credential_id,
                    uint64_t handshake_id, const uint8_t *device_id,
                    size_t device_id_length,
                    const uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE],
                    const uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE],
                    aiqa_pairing_random_fn random, void *random_context) {
  if (out_context == NULL || *out_context != NULL || !role_is_valid(role) ||
      !pairing_code_is_valid(pairing_code, pairing_code_length) ||
      credential_id == 0 || handshake_id == 0 || device_id == NULL ||
      device_id_length == 0 || device_id_length > AIQA_PAIRING_DEVICE_ID_MAX ||
      client_nonce == NULL || device_nonce == NULL || random == NULL) {
    return AIQA_PAIRING_INVALID_ARGUMENT;
  }
  aiqa_pairing_context_t *context = calloc(1, sizeof(*context));
  if (context == NULL) {
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  mbedtls_ecjpake_init(&context->pake);
  const mbedtls_ecjpake_role pake_role = role == AIQA_PAIRING_CLIENT
                                             ? MBEDTLS_ECJPAKE_CLIENT
                                             : MBEDTLS_ECJPAKE_SERVER;
  int result = mbedtls_ecjpake_setup(
      &context->pake, pake_role, MBEDTLS_MD_SHA256, MBEDTLS_ECP_DP_SECP256R1,
      pairing_code, pairing_code_length);
  if (result == 0) {
    result = mbedtls_ecjpake_set_point_format(&context->pake,
                                              MBEDTLS_ECP_PF_UNCOMPRESSED);
  }
  if (result != 0) {
    mbedtls_ecjpake_free(&context->pake);
    mbedtls_platform_zeroize(context, sizeof(*context));
    free(context);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  context->role = role;
  context->credential_id = credential_id;
  context->handshake_id = handshake_id;
  context->device_id_length = device_id_length;
  (void)memcpy(context->device_id, device_id, device_id_length);
  (void)memcpy(context->client_nonce, client_nonce, AIQA_PAIRING_NONCE_SIZE);
  (void)memcpy(context->device_nonce, device_nonce, AIQA_PAIRING_NONCE_SIZE);
  context->random = random;
  context->random_context = random_context;
  context->pake_active = true;
  *out_context = context;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t
aiqa_pairing_write_round_one(aiqa_pairing_context_t *context, uint8_t *output,
                             size_t output_capacity, size_t *out_length) {
  if (out_length != NULL)
    *out_length = 0;
  if (context == NULL || output == NULL || out_length == NULL)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (context->failed || context->own_round_one_written)
    return AIQA_PAIRING_INVALID_STATE;
  if (output_capacity < AIQA_PAIRING_ROUND_MAX)
    return AIQA_PAIRING_BUFFER_TOO_SMALL;
  const int result = mbedtls_ecjpake_write_round_one(
      &context->pake, output, output_capacity, out_length, context->random,
      context->random_context);
  if (result != 0 || *out_length == 0 || *out_length > AIQA_PAIRING_ROUND_MAX) {
    fail_context(context);
    *out_length = 0;
    mbedtls_platform_zeroize(output, output_capacity);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  store_round_for_role(context, context->role, 1, output, *out_length);
  context->own_round_one_written = true;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t
aiqa_pairing_read_round_one(aiqa_pairing_context_t *context,
                            const uint8_t *input, size_t input_length) {
  if (context == NULL || input == NULL || input_length == 0 ||
      input_length > AIQA_PAIRING_ROUND_MAX)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (context->failed || context->peer_round_one_read)
    return AIQA_PAIRING_INVALID_STATE;
  if (mbedtls_ecjpake_read_round_one(&context->pake, input, input_length) !=
      0) {
    fail_context(context);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  const aiqa_pairing_role_t peer_role = context->role == AIQA_PAIRING_CLIENT
                                            ? AIQA_PAIRING_DEVICE
                                            : AIQA_PAIRING_CLIENT;
  store_round_for_role(context, peer_role, 1, input, input_length);
  context->peer_round_one_read = true;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t
aiqa_pairing_write_round_two(aiqa_pairing_context_t *context, uint8_t *output,
                             size_t output_capacity, size_t *out_length) {
  if (out_length != NULL)
    *out_length = 0;
  if (context == NULL || output == NULL || out_length == NULL)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (context->failed || !context->own_round_one_written ||
      !context->peer_round_one_read || context->own_round_two_written)
    return AIQA_PAIRING_INVALID_STATE;
  if (output_capacity < AIQA_PAIRING_ROUND_MAX)
    return AIQA_PAIRING_BUFFER_TOO_SMALL;
  const int result = mbedtls_ecjpake_write_round_two(
      &context->pake, output, output_capacity, out_length, context->random,
      context->random_context);
  if (result != 0 || *out_length == 0 || *out_length > AIQA_PAIRING_ROUND_MAX) {
    fail_context(context);
    *out_length = 0;
    mbedtls_platform_zeroize(output, output_capacity);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  store_round_for_role(context, context->role, 2, output, *out_length);
  context->own_round_two_written = true;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t
aiqa_pairing_read_round_two(aiqa_pairing_context_t *context,
                            const uint8_t *input, size_t input_length) {
  if (context == NULL || input == NULL || input_length == 0 ||
      input_length > AIQA_PAIRING_ROUND_MAX)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (context->failed || !context->own_round_one_written ||
      !context->peer_round_one_read || context->peer_round_two_read)
    return AIQA_PAIRING_INVALID_STATE;
  if (mbedtls_ecjpake_read_round_two(&context->pake, input, input_length) !=
      0) {
    fail_context(context);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  const aiqa_pairing_role_t peer_role = context->role == AIQA_PAIRING_CLIENT
                                            ? AIQA_PAIRING_DEVICE
                                            : AIQA_PAIRING_CLIENT;
  store_round_for_role(context, peer_role, 2, input, input_length);
  context->peer_round_two_read = true;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t aiqa_pairing_derive_keys(aiqa_pairing_context_t *context,
                                               aiqa_pairing_keys_t **out_keys) {
  if (context == NULL || out_keys == NULL || *out_keys != NULL)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (context->failed || !context->own_round_two_written ||
      !context->peer_round_two_read || context->derived)
    return AIQA_PAIRING_INVALID_STATE;
  aiqa_pairing_keys_t *keys = calloc(1, sizeof(*keys));
  if (keys == NULL) {
    fail_context(context);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  aiqa_pairing_key_material_t *material = &keys->material;
  uint8_t shared[AIQA_PAIRING_SHARED_POINT_MAX] = {0};
  uint8_t salt_input[sizeof(SALT_LABEL) - 1U + AIQA_PAIRING_KEY_SIZE];
  uint8_t salt[AIQA_PAIRING_KEY_SIZE] = {0};
  uint8_t prk[AIQA_PAIRING_KEY_SIZE] = {0};
  size_t shared_length = 0;
  int result = transcript_hash(context, material->transcript_hash);
  if (result == 0) {
    result = mbedtls_ecjpake_write_shared_key(
        &context->pake, shared, sizeof(shared), &shared_length, context->random,
        context->random_context);
  }
  if (result == 0 && (shared_length != 65U || shared[0] != 0x04U))
    result = -1;
  (void)memcpy(salt_input, SALT_LABEL, sizeof(SALT_LABEL) - 1U);
  (void)memcpy(salt_input + sizeof(SALT_LABEL) - 1U, material->transcript_hash,
               AIQA_PAIRING_KEY_SIZE);
  if (result == 0)
    result = mbedtls_sha256(salt_input, sizeof(salt_input), salt, 0);
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (result == 0 && md == NULL)
    result = -1;
  if (result == 0)
    result = mbedtls_hkdf_extract(md, salt, sizeof(salt), shared, shared_length,
                                  prk);
  if (result == 0)
    result = expand_key(md, prk, "aiqa-management/v1 c2d aead key",
                        material->transcript_hash,
                        material->client_to_device_key, AIQA_PAIRING_KEY_SIZE);
  if (result == 0)
    result = expand_key(md, prk, "aiqa-management/v1 d2c aead key",
                        material->transcript_hash,
                        material->device_to_client_key, AIQA_PAIRING_KEY_SIZE);
  if (result == 0)
    result = expand_key(md, prk, "aiqa-management/v1 c2d nonce prefix",
                        material->transcript_hash,
                        material->client_to_device_nonce_prefix,
                        AIQA_PAIRING_NONCE_PREFIX_SIZE);
  if (result == 0)
    result = expand_key(md, prk, "aiqa-management/v1 d2c nonce prefix",
                        material->transcript_hash,
                        material->device_to_client_nonce_prefix,
                        AIQA_PAIRING_NONCE_PREFIX_SIZE);
  if (result == 0)
    result = expand_key(md, prk, "aiqa-management/v1 client finished",
                        material->transcript_hash,
                        material->client_finished_key, AIQA_PAIRING_KEY_SIZE);
  if (result == 0)
    result = expand_key(md, prk, "aiqa-management/v1 device finished",
                        material->transcript_hash,
                        material->device_finished_key, AIQA_PAIRING_KEY_SIZE);
  if (result == 0)
    result = expand_key(md, prk, "aiqa-management/v1 session id",
                        material->transcript_hash, material->session_id,
                        AIQA_PAIRING_SESSION_ID_SIZE);
  mbedtls_platform_zeroize(shared, sizeof(shared));
  mbedtls_platform_zeroize(salt_input, sizeof(salt_input));
  mbedtls_platform_zeroize(salt, sizeof(salt));
  mbedtls_platform_zeroize(prk, sizeof(prk));
  if (result != 0) {
    fail_context(context);
    aiqa_pairing_keys_destroy(&keys);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  mbedtls_ecjpake_free(&context->pake);
  context->pake_active = false;
  context->derived = true;
  keys->local_role = context->role;
  *out_keys = keys;
  return AIQA_PAIRING_OK;
}

static aiqa_pairing_result_t
make_finished(const aiqa_pairing_keys_t *keys, aiqa_pairing_role_t sender,
              uint8_t output[AIQA_PAIRING_FINISHED_SIZE]) {
  (void)memset(output, 0, AIQA_PAIRING_FINISHED_SIZE);
  const uint8_t *key = sender == AIQA_PAIRING_CLIENT
                           ? keys->material.client_finished_key
                           : keys->material.device_finished_key;
  const char *label = sender == AIQA_PAIRING_CLIENT ? "client finished proof"
                                                    : "device finished proof";
  uint8_t message[64];
  const size_t label_length = strlen(label);
  (void)memcpy(message, label, label_length);
  (void)memcpy(message + label_length, keys->material.transcript_hash,
               AIQA_PAIRING_KEY_SIZE);
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  const int result =
      md == NULL
          ? -1
          : mbedtls_md_hmac(md, key, AIQA_PAIRING_KEY_SIZE, message,
                            label_length + AIQA_PAIRING_KEY_SIZE, output);
  mbedtls_platform_zeroize(message, sizeof(message));
  if (result != 0) {
    mbedtls_platform_zeroize(output, AIQA_PAIRING_FINISHED_SIZE);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t
aiqa_pairing_create_finished(aiqa_pairing_keys_t *keys,
                             aiqa_pairing_role_t sender,
                             uint8_t output[AIQA_PAIRING_FINISHED_SIZE]) {
  if (output != NULL)
    mbedtls_platform_zeroize(output, AIQA_PAIRING_FINISHED_SIZE);
  if (keys == NULL || output == NULL || !role_is_valid(sender))
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (keys->failed || sender != keys->local_role ||
      keys->local_finished_created)
    return AIQA_PAIRING_INVALID_STATE;
  const aiqa_pairing_result_t result = make_finished(keys, sender, output);
  if (result == AIQA_PAIRING_OK)
    keys->local_finished_created = true;
  return result;
}

aiqa_pairing_result_t aiqa_pairing_verify_finished(aiqa_pairing_keys_t *keys,
                                                   aiqa_pairing_role_t sender,
                                                   const uint8_t *tag,
                                                   size_t tag_length) {
  if (keys == NULL || tag == NULL || tag_length != AIQA_PAIRING_FINISHED_SIZE ||
      !role_is_valid(sender))
    return AIQA_PAIRING_INVALID_ARGUMENT;
  const aiqa_pairing_role_t peer_role = keys->local_role == AIQA_PAIRING_CLIENT
                                            ? AIQA_PAIRING_DEVICE
                                            : AIQA_PAIRING_CLIENT;
  if (keys->failed || sender != peer_role || keys->peer_finished_verified)
    return AIQA_PAIRING_INVALID_STATE;
  uint8_t expected[AIQA_PAIRING_FINISHED_SIZE] = {0};
  const aiqa_pairing_result_t result = make_finished(keys, sender, expected);
  if (result != AIQA_PAIRING_OK) {
    mbedtls_platform_zeroize(expected, sizeof(expected));
    return result;
  }
  const int difference = mbedtls_ct_memcmp(expected, tag, sizeof(expected));
  mbedtls_platform_zeroize(expected, sizeof(expected));
  if (difference != 0) {
    keys->failed = true;
    mbedtls_platform_zeroize(&keys->material, sizeof(keys->material));
    return AIQA_PAIRING_AUTH_FAILED;
  }
  keys->peer_finished_verified = true;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t
aiqa_pairing_consume_confirmed_keys(aiqa_pairing_keys_t **keys,
                                    aiqa_pairing_role_t *out_local_role,
                                    aiqa_pairing_key_material_t *out_material) {
  if (keys == NULL || *keys == NULL || out_local_role == NULL ||
      out_material == NULL)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if ((*keys)->failed || !(*keys)->local_finished_created ||
      !(*keys)->peer_finished_verified)
    return AIQA_PAIRING_INVALID_STATE;
  *out_local_role = (*keys)->local_role;
  (void)memcpy(out_material, &(*keys)->material, sizeof(*out_material));
  aiqa_pairing_keys_destroy(keys);
  return AIQA_PAIRING_OK;
}

void aiqa_pairing_keys_destroy(aiqa_pairing_keys_t **keys) {
  if (keys == NULL || *keys == NULL)
    return;
  mbedtls_platform_zeroize(*keys, sizeof(**keys));
  free(*keys);
  *keys = NULL;
}

void aiqa_pairing_destroy(aiqa_pairing_context_t **context) {
  if (context == NULL || *context == NULL)
    return;
  if ((*context)->pake_active)
    mbedtls_ecjpake_free(&(*context)->pake);
  mbedtls_platform_zeroize(*context, sizeof(**context));
  free(*context);
  *context = NULL;
}
