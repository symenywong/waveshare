#include "aiqa_secure_channel.h"
#include "aiqa_pairing_crypto_internal.h"

#include "mbedtls/constant_time.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t RECORD_MAGIC[] = {'A', 'Q', 'S', 'E'};

struct aiqa_secure_channel {
  mbedtls_gcm_context send_gcm;
  mbedtls_gcm_context receive_gcm;
  aiqa_pairing_role_t local_role;
  uint8_t send_nonce_prefix[AIQA_PAIRING_NONCE_PREFIX_SIZE];
  uint8_t receive_nonce_prefix[AIQA_PAIRING_NONCE_PREFIX_SIZE];
  uint8_t session_id[AIQA_PAIRING_SESSION_ID_SIZE];
  uint64_t send_counter;
  uint64_t receive_counter;
};

static bool frame_kind_is_valid(uint8_t kind) {
  return kind >= AIQA_SECURE_FRAME_REQUEST && kind <= AIQA_SECURE_FRAME_EVENT;
}

static bool regions_overlap(const void *first, size_t first_length,
                            const void *second, size_t second_length) {
  if (first_length == 0 || second_length == 0)
    return false;
  const uintptr_t first_address = (uintptr_t)first;
  const uintptr_t second_address = (uintptr_t)second;
  return first_address <= second_address
             ? second_address - first_address < first_length
             : first_address - second_address < second_length;
}

static bool send_kind_is_valid(aiqa_pairing_role_t role, uint8_t kind) {
  return role == AIQA_PAIRING_CLIENT ? kind == AIQA_SECURE_FRAME_REQUEST
                                     : kind == AIQA_SECURE_FRAME_RESPONSE ||
                                           kind == AIQA_SECURE_FRAME_EVENT;
}

static bool receive_kind_is_valid(aiqa_pairing_role_t role, uint8_t kind) {
  return role == AIQA_PAIRING_CLIENT ? kind == AIQA_SECURE_FRAME_RESPONSE ||
                                           kind == AIQA_SECURE_FRAME_EVENT
                                     : kind == AIQA_SECURE_FRAME_REQUEST;
}

static aiqa_pairing_result_t reject_record(uint8_t *plaintext,
                                           size_t plaintext_capacity,
                                           aiqa_pairing_result_t result) {
  if (plaintext != NULL && plaintext_capacity > 0)
    mbedtls_platform_zeroize(plaintext, plaintext_capacity);
  return result;
}

static void write_u32_be(uint8_t output[4], uint32_t value) {
  output[0] = (uint8_t)(value >> 24U);
  output[1] = (uint8_t)(value >> 16U);
  output[2] = (uint8_t)(value >> 8U);
  output[3] = (uint8_t)value;
}

static void write_u64_be(uint8_t output[8], uint64_t value) {
  for (size_t index = 0; index < 8; ++index)
    output[7U - index] = (uint8_t)(value >> (index * 8U));
}

static uint32_t read_u32_be(const uint8_t input[4]) {
  return ((uint32_t)input[0] << 24U) | ((uint32_t)input[1] << 16U) |
         ((uint32_t)input[2] << 8U) | (uint32_t)input[3];
}

static uint64_t read_u64_be(const uint8_t input[8]) {
  uint64_t value = 0;
  for (size_t index = 0; index < 8; ++index)
    value = (value << 8U) | input[index];
  return value;
}

static void make_nonce(const uint8_t prefix[AIQA_PAIRING_NONCE_PREFIX_SIZE],
                       uint64_t counter, uint8_t output[12]) {
  (void)memcpy(output, prefix, AIQA_PAIRING_NONCE_PREFIX_SIZE);
  write_u64_be(output + AIQA_PAIRING_NONCE_PREFIX_SIZE, counter);
}

static void make_outer_header(uint8_t kind, size_t payload_length,
                              uint8_t output[12]) {
  (void)memcpy(output, "AQMG", 4);
  output[4] = 1;
  output[5] = kind;
  output[6] = 0;
  output[7] = 0;
  write_u32_be(output + 8, (uint32_t)payload_length);
}

aiqa_pairing_result_t
aiqa_secure_channel_create(aiqa_secure_channel_t **out_channel,
                           aiqa_pairing_keys_t **confirmed_keys) {
  if (out_channel == NULL || *out_channel != NULL || confirmed_keys == NULL ||
      *confirmed_keys == NULL)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  aiqa_secure_channel_t *channel = calloc(1, sizeof(*channel));
  if (channel == NULL)
    return AIQA_PAIRING_CRYPTO_ERROR;
  mbedtls_gcm_init(&channel->send_gcm);
  mbedtls_gcm_init(&channel->receive_gcm);
  aiqa_pairing_key_material_t material = {0};
  aiqa_pairing_role_t local_role = 0;
  const aiqa_pairing_result_t consume_result =
      aiqa_pairing_consume_confirmed_keys(confirmed_keys, &local_role,
                                          &material);
  if (consume_result != AIQA_PAIRING_OK) {
    aiqa_secure_channel_destroy(&channel);
    return consume_result;
  }
  const uint8_t *send_key = local_role == AIQA_PAIRING_CLIENT
                                ? material.client_to_device_key
                                : material.device_to_client_key;
  const uint8_t *receive_key = local_role == AIQA_PAIRING_CLIENT
                                   ? material.device_to_client_key
                                   : material.client_to_device_key;
  const uint8_t *send_prefix = local_role == AIQA_PAIRING_CLIENT
                                   ? material.client_to_device_nonce_prefix
                                   : material.device_to_client_nonce_prefix;
  const uint8_t *receive_prefix = local_role == AIQA_PAIRING_CLIENT
                                      ? material.device_to_client_nonce_prefix
                                      : material.client_to_device_nonce_prefix;
  int result = mbedtls_gcm_setkey(&channel->send_gcm, MBEDTLS_CIPHER_ID_AES,
                                  send_key, 256);
  if (result == 0)
    result = mbedtls_gcm_setkey(&channel->receive_gcm, MBEDTLS_CIPHER_ID_AES,
                                receive_key, 256);
  if (result != 0) {
    mbedtls_platform_zeroize(&material, sizeof(material));
    aiqa_secure_channel_destroy(&channel);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  channel->local_role = local_role;
  (void)memcpy(channel->send_nonce_prefix, send_prefix,
               AIQA_PAIRING_NONCE_PREFIX_SIZE);
  (void)memcpy(channel->receive_nonce_prefix, receive_prefix,
               AIQA_PAIRING_NONCE_PREFIX_SIZE);
  (void)memcpy(channel->session_id, material.session_id,
               AIQA_PAIRING_SESSION_ID_SIZE);
  mbedtls_platform_zeroize(&material, sizeof(material));
  channel->send_counter = 1;
  channel->receive_counter = 1;
  *out_channel = channel;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t
aiqa_secure_channel_encrypt(aiqa_secure_channel_t *channel,
                            uint8_t outer_frame_kind, const uint8_t *plaintext,
                            size_t plaintext_length, uint8_t *output,
                            size_t output_capacity, size_t *out_length) {
  if (out_length != NULL)
    *out_length = 0;
  if (channel == NULL || plaintext == NULL || output == NULL ||
      out_length == NULL || !frame_kind_is_valid(outer_frame_kind))
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (!send_kind_is_valid(channel->local_role, outer_frame_kind))
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (plaintext_length > AIQA_SECURE_PLAINTEXT_MAX)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  const size_t record_length = AIQA_SECURE_RECORD_HEADER_SIZE +
                               plaintext_length + AIQA_SECURE_RECORD_TAG_SIZE;
  if (regions_overlap(plaintext, plaintext_length, output, record_length))
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (output_capacity < record_length)
    return AIQA_PAIRING_BUFFER_TOO_SMALL;
  if (channel->send_counter == UINT64_MAX)
    return AIQA_PAIRING_INVALID_STATE;
  (void)memcpy(output, RECORD_MAGIC, sizeof(RECORD_MAGIC));
  output[4] = 1;
  output[5] = (uint8_t)channel->local_role;
  output[6] = 0;
  output[7] = 0;
  (void)memcpy(output + 8, channel->session_id, AIQA_PAIRING_SESSION_ID_SIZE);
  write_u64_be(output + 16, channel->send_counter);
  write_u32_be(output + 24, (uint32_t)plaintext_length);
  uint8_t nonce[12];
  uint8_t aad[12 + AIQA_SECURE_RECORD_HEADER_SIZE];
  make_nonce(channel->send_nonce_prefix, channel->send_counter, nonce);
  make_outer_header(outer_frame_kind, record_length, aad);
  (void)memcpy(aad + 12, output, AIQA_SECURE_RECORD_HEADER_SIZE);
  const int result = mbedtls_gcm_crypt_and_tag(
      &channel->send_gcm, MBEDTLS_GCM_ENCRYPT, plaintext_length, nonce,
      sizeof(nonce), aad, sizeof(aad), plaintext,
      output + AIQA_SECURE_RECORD_HEADER_SIZE, AIQA_SECURE_RECORD_TAG_SIZE,
      output + AIQA_SECURE_RECORD_HEADER_SIZE + plaintext_length);
  mbedtls_platform_zeroize(nonce, sizeof(nonce));
  mbedtls_platform_zeroize(aad, sizeof(aad));
  if (result != 0) {
    mbedtls_platform_zeroize(output, record_length);
    return AIQA_PAIRING_CRYPTO_ERROR;
  }
  channel->send_counter += 1U;
  *out_length = record_length;
  return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t aiqa_secure_channel_decrypt(
    aiqa_secure_channel_t *channel, uint8_t outer_frame_kind,
    const uint8_t *record, size_t record_length, uint8_t *plaintext,
    size_t plaintext_capacity, size_t *out_plaintext_length) {
  if (out_plaintext_length != NULL)
    *out_plaintext_length = 0;
  const bool buffers_overlap =
      record != NULL && plaintext != NULL &&
      regions_overlap(record, record_length, plaintext, plaintext_capacity);
  if (plaintext != NULL && plaintext_capacity > 0)
    mbedtls_platform_zeroize(plaintext, plaintext_capacity);
  if (channel == NULL || record == NULL || plaintext == NULL ||
      out_plaintext_length == NULL || !frame_kind_is_valid(outer_frame_kind))
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (buffers_overlap)
    return AIQA_PAIRING_INVALID_ARGUMENT;
  if (!receive_kind_is_valid(channel->local_role, outer_frame_kind))
    return reject_record(plaintext, plaintext_capacity,
                         AIQA_PAIRING_AUTH_FAILED);
  if (record_length <
          AIQA_SECURE_RECORD_HEADER_SIZE + AIQA_SECURE_RECORD_TAG_SIZE ||
      record_length > AIQA_SECURE_RECORD_MAX ||
      memcmp(record, RECORD_MAGIC, sizeof(RECORD_MAGIC)) != 0 ||
      record[4] != 1 || record[6] != 0 || record[7] != 0)
    return reject_record(plaintext, plaintext_capacity,
                         AIQA_PAIRING_AUTH_FAILED);
  const uint8_t expected_peer_role = channel->local_role == AIQA_PAIRING_CLIENT
                                         ? AIQA_PAIRING_DEVICE
                                         : AIQA_PAIRING_CLIENT;
  if (record[5] != expected_peer_role ||
      mbedtls_ct_memcmp(record + 8, channel->session_id,
                        AIQA_PAIRING_SESSION_ID_SIZE) != 0)
    return reject_record(plaintext, plaintext_capacity,
                         AIQA_PAIRING_AUTH_FAILED);
  const uint32_t ciphertext_length = read_u32_be(record + 24);
  if (ciphertext_length > AIQA_SECURE_PLAINTEXT_MAX ||
      (size_t)ciphertext_length + AIQA_SECURE_RECORD_HEADER_SIZE +
              AIQA_SECURE_RECORD_TAG_SIZE !=
          record_length ||
      ciphertext_length > plaintext_capacity)
    return reject_record(plaintext, plaintext_capacity,
                         AIQA_PAIRING_AUTH_FAILED);
  const uint64_t counter = read_u64_be(record + 16);
  if (counter != channel->receive_counter || counter == UINT64_MAX)
    return reject_record(plaintext, plaintext_capacity, AIQA_PAIRING_REPLAY);
  uint8_t nonce[12];
  uint8_t aad[12 + AIQA_SECURE_RECORD_HEADER_SIZE];
  make_nonce(channel->receive_nonce_prefix, counter, nonce);
  make_outer_header(outer_frame_kind, record_length, aad);
  (void)memcpy(aad + 12, record, AIQA_SECURE_RECORD_HEADER_SIZE);
  const int result = mbedtls_gcm_auth_decrypt(
      &channel->receive_gcm, ciphertext_length, nonce, sizeof(nonce), aad,
      sizeof(aad), record + AIQA_SECURE_RECORD_HEADER_SIZE + ciphertext_length,
      AIQA_SECURE_RECORD_TAG_SIZE, record + AIQA_SECURE_RECORD_HEADER_SIZE,
      plaintext);
  mbedtls_platform_zeroize(nonce, sizeof(nonce));
  mbedtls_platform_zeroize(aad, sizeof(aad));
  if (result != 0) {
    return reject_record(plaintext, plaintext_capacity,
                         AIQA_PAIRING_AUTH_FAILED);
  }
  channel->receive_counter += 1U;
  *out_plaintext_length = ciphertext_length;
  return AIQA_PAIRING_OK;
}

void aiqa_secure_channel_destroy(aiqa_secure_channel_t **channel) {
  if (channel == NULL || *channel == NULL)
    return;
  mbedtls_gcm_free(&(*channel)->send_gcm);
  mbedtls_gcm_free(&(*channel)->receive_gcm);
  mbedtls_platform_zeroize(*channel, sizeof(**channel));
  free(*channel);
  *channel = NULL;
}
