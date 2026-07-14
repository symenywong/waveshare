#include "aiqa_management_wire.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    unsigned count;
    aiqa_management_wire_kind_t kind;
    uint8_t payload[AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD];
    size_t payload_length;
} capture_t;

static void capture_frame(
    void *opaque,
    aiqa_management_wire_kind_t kind,
    const uint8_t *payload,
    size_t payload_length)
{
    capture_t *capture = opaque;
    capture->count += 1U;
    capture->kind = kind;
    capture->payload_length = payload_length;
    if (payload_length > 0) {
        (void)memcpy(capture->payload, payload, payload_length);
    }
}

static size_t make_frame(
    aiqa_management_wire_kind_t kind,
    const char *payload,
    uint8_t *out,
    size_t capacity)
{
    const size_t payload_length = strlen(payload);
    assert(capacity >= AIQA_MANAGEMENT_WIRE_HEADER_SIZE + payload_length);
    assert(aiqa_management_wire_encode_header(
        kind, payload_length, out, AIQA_MANAGEMENT_WIRE_HEADER_SIZE));
    (void)memcpy(out + AIQA_MANAGEMENT_WIRE_HEADER_SIZE, payload, payload_length);
    return AIQA_MANAGEMENT_WIRE_HEADER_SIZE + payload_length;
}

static void run_encode(void)
{
    uint8_t header[AIQA_MANAGEMENT_WIRE_HEADER_SIZE] = {0};
    assert(aiqa_management_wire_encode_header(
        AIQA_MANAGEMENT_WIRE_REQUEST, 0x0102U, header, sizeof(header)));
    assert(memcmp(header, "AQMG", 4) == 0);
    assert(header[4] == AIQA_MANAGEMENT_WIRE_VERSION);
    assert(header[5] == AIQA_MANAGEMENT_WIRE_REQUEST);
    assert(header[6] == 0 && header[7] == 0);
    assert(header[8] == 0 && header[9] == 0 && header[10] == 1 && header[11] == 2);
    assert(!aiqa_management_wire_encode_header(
        (aiqa_management_wire_kind_t)99, 1, header, sizeof(header)));
    assert(!aiqa_management_wire_encode_header(
        AIQA_MANAGEMENT_WIRE_REQUEST,
        AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD + 1U,
        header,
        sizeof(header)));
}

static void run_partial(void)
{
    uint8_t frame[128] = {0};
    const char *payload = "{\"id\":1,\"method\":\"system.hello\"}";
    const size_t frame_length = make_frame(
        AIQA_MANAGEMENT_WIRE_REQUEST, payload, frame, sizeof(frame));
    aiqa_management_wire_decoder_t decoder = {0};
    aiqa_management_wire_decoder_init(&decoder);
    capture_t capture = {0};
    for (size_t offset = 0; offset < frame_length; ++offset) {
        assert(aiqa_management_wire_decoder_feed(
            &decoder, &frame[offset], 1, capture_frame, &capture));
    }
    assert(capture.count == 1);
    assert(capture.kind == AIQA_MANAGEMENT_WIRE_REQUEST);
    assert(capture.payload_length == strlen(payload));
    assert(memcmp(capture.payload, payload, capture.payload_length) == 0);
}

static void run_coalesced(void)
{
    uint8_t frames[256] = {0};
    size_t length = make_frame(
        AIQA_MANAGEMENT_WIRE_REQUEST, "{}", frames, sizeof(frames));
    length += make_frame(
        AIQA_MANAGEMENT_WIRE_RESPONSE,
        "{\"ok\":true}",
        frames + length,
        sizeof(frames) - length);
    aiqa_management_wire_decoder_t decoder = {0};
    aiqa_management_wire_decoder_init(&decoder);
    capture_t capture = {0};
    assert(aiqa_management_wire_decoder_feed(
        &decoder, frames, length, capture_frame, &capture));
    assert(capture.count == 2);
    assert(capture.kind == AIQA_MANAGEMENT_WIRE_RESPONSE);
}

static void run_resync(void)
{
    uint8_t stream[160] = {'n', 'o', 'i', 's', 'e'};
    const char *payload = "{\"id\":7}";
    const size_t frame_length = make_frame(
        AIQA_MANAGEMENT_WIRE_REQUEST,
        payload,
        stream + 5,
        sizeof(stream) - 5);
    aiqa_management_wire_decoder_t decoder = {0};
    aiqa_management_wire_decoder_init(&decoder);
    capture_t capture = {0};
    assert(aiqa_management_wire_decoder_feed(
        &decoder, stream, frame_length + 5, capture_frame, &capture));
    assert(capture.count == 1);
    assert(aiqa_management_wire_decoder_dropped_bytes(&decoder) == 5);
}

static void run_oversize(void)
{
    uint8_t header[AIQA_MANAGEMENT_WIRE_HEADER_SIZE] = {
        'A', 'Q', 'M', 'G', AIQA_MANAGEMENT_WIRE_VERSION,
        AIQA_MANAGEMENT_WIRE_REQUEST, 0, 0, 0, 0, 0x10, 0x01};
    aiqa_management_wire_decoder_t decoder = {0};
    aiqa_management_wire_decoder_init(&decoder);
    capture_t capture = {0};
    assert(!aiqa_management_wire_decoder_feed(
        &decoder, header, sizeof(header), capture_frame, &capture));
    assert(capture.count == 0);
    assert(aiqa_management_wire_decoder_protocol_errors(&decoder) == 1);
}

static void run_empty(void)
{
    uint8_t header[AIQA_MANAGEMENT_WIRE_HEADER_SIZE] = {0};
    assert(aiqa_management_wire_encode_header(
        AIQA_MANAGEMENT_WIRE_EVENT, 0, header, sizeof(header)));
    aiqa_management_wire_decoder_t decoder = {0};
    capture_t capture = {0};
    aiqa_management_wire_decoder_init(&decoder);
    assert(aiqa_management_wire_decoder_feed(
        &decoder, header, sizeof(header), capture_frame, &capture));
    assert(capture.count == 1);
    assert(capture.kind == AIQA_MANAGEMENT_WIRE_EVENT);
    assert(capture.payload_length == 0);
}

static void run_invalid(void)
{
    uint8_t header[AIQA_MANAGEMENT_WIRE_HEADER_SIZE] = {0};
    assert(!aiqa_management_wire_encode_header(
        AIQA_MANAGEMENT_WIRE_REQUEST, 0, NULL, sizeof(header)));
    assert(!aiqa_management_wire_encode_header(
        AIQA_MANAGEMENT_WIRE_REQUEST, 0, header, sizeof(header) - 1U));

    aiqa_management_wire_decoder_t decoder = {0};
    capture_t capture = {0};
    aiqa_management_wire_decoder_init(&decoder);
    assert(!aiqa_management_wire_decoder_feed(
        NULL, header, sizeof(header), capture_frame, &capture));
    assert(!aiqa_management_wire_decoder_feed(
        &decoder, NULL, 1, capture_frame, &capture));
    assert(!aiqa_management_wire_decoder_feed(
        &decoder, header, sizeof(header), NULL, &capture));
    assert(aiqa_management_wire_decoder_dropped_bytes(NULL) == 0);
    assert(aiqa_management_wire_decoder_protocol_errors(NULL) == 0);
    aiqa_management_wire_decoder_secure_clear(NULL);

    const size_t corrupt_offsets[] = {4, 5, 6, 7};
    for (size_t index = 0; index < sizeof(corrupt_offsets) / sizeof(corrupt_offsets[0]);
         ++index) {
        assert(aiqa_management_wire_encode_header(
            AIQA_MANAGEMENT_WIRE_REQUEST, 0, header, sizeof(header)));
        header[corrupt_offsets[index]] = 0x7f;
        aiqa_management_wire_decoder_init(&decoder);
        assert(!aiqa_management_wire_decoder_feed(
            &decoder, header, sizeof(header), capture_frame, &capture));
        assert(aiqa_management_wire_decoder_protocol_errors(&decoder) == 1);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "encode") == 0) {
        run_encode();
    } else if (strcmp(argv[1], "partial") == 0) {
        run_partial();
    } else if (strcmp(argv[1], "coalesced") == 0) {
        run_coalesced();
    } else if (strcmp(argv[1], "resync") == 0) {
        run_resync();
    } else if (strcmp(argv[1], "oversize") == 0) {
        run_oversize();
    } else if (strcmp(argv[1], "empty") == 0) {
        run_empty();
    } else if (strcmp(argv[1], "invalid") == 0) {
        run_invalid();
    } else {
        return 2;
    }
    return 0;
}
