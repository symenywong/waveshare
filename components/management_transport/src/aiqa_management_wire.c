#include "aiqa_management_wire.h"

#include <string.h>

static const uint8_t MAGIC[] = {'A', 'Q', 'M', 'G'};

static bool kind_is_valid(aiqa_management_wire_kind_t kind)
{
    return kind == AIQA_MANAGEMENT_WIRE_REQUEST ||
           kind == AIQA_MANAGEMENT_WIRE_RESPONSE ||
           kind == AIQA_MANAGEMENT_WIRE_EVENT;
}

static void secure_zero(void *value, size_t value_size)
{
    volatile uint8_t *bytes = value;
    while (value_size > 0) {
        *bytes++ = 0;
        --value_size;
    }
}

static void reset_frame(aiqa_management_wire_decoder_t *decoder)
{
    secure_zero(decoder->header, sizeof(decoder->header));
    if (decoder->payload_used > 0) {
        secure_zero(decoder->payload, decoder->payload_used);
    }
    decoder->header_used = 0;
    decoder->payload_used = 0;
    decoder->expected_payload_length = 0;
}

static uint32_t decode_u32_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

bool aiqa_management_wire_encode_header(
    aiqa_management_wire_kind_t kind,
    size_t payload_length,
    uint8_t *out_header,
    size_t header_capacity)
{
    if (!kind_is_valid(kind) || payload_length > AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD ||
        out_header == NULL || header_capacity < AIQA_MANAGEMENT_WIRE_HEADER_SIZE) {
        return false;
    }
    const uint32_t length = (uint32_t)payload_length;
    (void)memcpy(out_header, MAGIC, sizeof(MAGIC));
    out_header[4] = AIQA_MANAGEMENT_WIRE_VERSION;
    out_header[5] = (uint8_t)kind;
    out_header[6] = 0;
    out_header[7] = 0;
    out_header[8] = (uint8_t)(length >> 24U);
    out_header[9] = (uint8_t)(length >> 16U);
    out_header[10] = (uint8_t)(length >> 8U);
    out_header[11] = (uint8_t)length;
    return true;
}

void aiqa_management_wire_decoder_init(aiqa_management_wire_decoder_t *decoder)
{
    if (decoder != NULL) {
        secure_zero(decoder, sizeof(*decoder));
    }
}

static bool consume_header_byte(aiqa_management_wire_decoder_t *decoder, uint8_t byte)
{
    if (decoder->header_used < sizeof(MAGIC)) {
        const size_t magic_offset = decoder->header_used;
        if (byte == MAGIC[magic_offset]) {
            decoder->header[decoder->header_used++] = byte;
        } else {
            decoder->dropped_bytes += 1U;
            decoder->header_used = byte == MAGIC[0] ? 1U : 0U;
            if (decoder->header_used == 1U) {
                decoder->header[0] = byte;
            }
        }
        return true;
    }

    decoder->header[decoder->header_used++] = byte;
    if (decoder->header_used < AIQA_MANAGEMENT_WIRE_HEADER_SIZE) {
        return true;
    }

    const aiqa_management_wire_kind_t kind =
        (aiqa_management_wire_kind_t)decoder->header[5];
    const uint32_t payload_length = decode_u32_be(&decoder->header[8]);
    if (decoder->header[4] != AIQA_MANAGEMENT_WIRE_VERSION ||
        !kind_is_valid(kind) || decoder->header[6] != 0 || decoder->header[7] != 0 ||
        payload_length > AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD) {
        decoder->protocol_errors += 1U;
        reset_frame(decoder);
        return false;
    }
    decoder->expected_payload_length = payload_length;
    return true;
}

bool aiqa_management_wire_decoder_feed(
    aiqa_management_wire_decoder_t *decoder,
    const uint8_t *data,
    size_t data_length,
    aiqa_management_wire_frame_callback_t on_frame,
    void *callback_context)
{
    if (decoder == NULL || (data == NULL && data_length > 0) || on_frame == NULL) {
        return false;
    }
    for (size_t offset = 0; offset < data_length; ++offset) {
        if (decoder->header_used < AIQA_MANAGEMENT_WIRE_HEADER_SIZE) {
            if (!consume_header_byte(decoder, data[offset])) {
                return false;
            }
            if (decoder->header_used == AIQA_MANAGEMENT_WIRE_HEADER_SIZE &&
                decoder->expected_payload_length == 0) {
                const aiqa_management_wire_kind_t kind =
                    (aiqa_management_wire_kind_t)decoder->header[5];
                on_frame(callback_context, kind, decoder->payload, 0);
                reset_frame(decoder);
            }
            continue;
        }

        decoder->payload[decoder->payload_used++] = data[offset];
        if (decoder->payload_used == decoder->expected_payload_length) {
            const aiqa_management_wire_kind_t kind =
                (aiqa_management_wire_kind_t)decoder->header[5];
            on_frame(
                callback_context, kind, decoder->payload, decoder->payload_used);
            reset_frame(decoder);
        }
    }
    return true;
}

uint64_t aiqa_management_wire_decoder_dropped_bytes(
    const aiqa_management_wire_decoder_t *decoder)
{
    return decoder != NULL ? decoder->dropped_bytes : 0;
}

uint32_t aiqa_management_wire_decoder_protocol_errors(
    const aiqa_management_wire_decoder_t *decoder)
{
    return decoder != NULL ? decoder->protocol_errors : 0;
}

void aiqa_management_wire_decoder_secure_clear(aiqa_management_wire_decoder_t *decoder)
{
    if (decoder != NULL) {
        secure_zero(decoder, sizeof(*decoder));
    }
}
