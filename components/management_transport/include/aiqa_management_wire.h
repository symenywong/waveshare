#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_MANAGEMENT_WIRE_VERSION 1U
#define AIQA_MANAGEMENT_WIRE_HEADER_SIZE 12U
#define AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD 4096U

typedef enum {
    AIQA_MANAGEMENT_WIRE_REQUEST = 1,
    AIQA_MANAGEMENT_WIRE_RESPONSE = 2,
    AIQA_MANAGEMENT_WIRE_EVENT = 3,
} aiqa_management_wire_kind_t;

typedef void (*aiqa_management_wire_frame_callback_t)(
    void *context,
    aiqa_management_wire_kind_t kind,
    const uint8_t *payload,
    size_t payload_length);

typedef struct {
    uint8_t header[AIQA_MANAGEMENT_WIRE_HEADER_SIZE];
    size_t header_used;
    uint8_t payload[AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD];
    size_t payload_used;
    size_t expected_payload_length;
    uint64_t dropped_bytes;
    uint32_t protocol_errors;
} aiqa_management_wire_decoder_t;

bool aiqa_management_wire_encode_header(
    aiqa_management_wire_kind_t kind,
    size_t payload_length,
    uint8_t *out_header,
    size_t header_capacity);

void aiqa_management_wire_decoder_init(aiqa_management_wire_decoder_t *decoder);

/* Returns false for a fatal frame-header violation; caller should reset the link decoder. */
bool aiqa_management_wire_decoder_feed(
    aiqa_management_wire_decoder_t *decoder,
    const uint8_t *data,
    size_t data_length,
    aiqa_management_wire_frame_callback_t on_frame,
    void *callback_context);

uint64_t aiqa_management_wire_decoder_dropped_bytes(
    const aiqa_management_wire_decoder_t *decoder);
uint32_t aiqa_management_wire_decoder_protocol_errors(
    const aiqa_management_wire_decoder_t *decoder);
void aiqa_management_wire_decoder_secure_clear(aiqa_management_wire_decoder_t *decoder);

#ifdef __cplusplus
}
#endif
