#include "aiqa_management_protocol.h"
#include "aiqa_management_wire.h"
#include "cJSON.h"

#include <assert.h>
#include <string.h>

static unsigned s_diagnostics_calls;

static bool copy_diagnostics(
    void *context,
    aiqa_management_public_diagnostics_t *out_diagnostics)
{
    assert(context == (void *)0x1234);
    assert(out_diagnostics != NULL);
    s_diagnostics_calls += 1U;
    *out_diagnostics = (aiqa_management_public_diagnostics_t){
        .runtime_ready = true,
        .runtime_start_phase = 6U,
        .runtime_start_status = 0,
        .board_init_phase = 19U,
        .generation = 17U,
        .state = AIQA_STATE_TRANSCRIBING,
        .error = AIQA_ERROR_NONE,
        .state_sequence = 29U,
        .asr = {
            .request_epoch = 31U,
            .phase = 3U,
            .status = 4,
            .http_status = 408,
            .transport_status = -1,
            .socket_errno = 116,
            .content_length = -28679,
            .pcm_bytes = 96000U,
            .post_bytes = 128123U,
            .uploaded_bytes = 128123U,
            .upload_write_calls = 43U,
            .response_bytes = 0U,
            .response_limit = 4095U,
            .header_wait_ms = 5001U,
            .elapsed_ms = 5210U,
            .response_complete = false,
        },
    };
    return true;
}

static void run_hello(void)
{
    const char request[] = "{\"id\":1,\"method\":\"system.hello\"}";
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    size_t response_length = 0;
    assert(aiqa_management_protocol_handle_public_request(
        (const uint8_t *)request,
        strlen(request),
        response,
        sizeof(response),
        &response_length));
    assert(response_length == strlen(response));
    const char *parse_end = NULL;
    cJSON *parsed = cJSON_ParseWithLengthOpts(
        response, response_length, &parse_end, false);
    assert(parsed != NULL);
    assert(parse_end == response + response_length);
    cJSON_Delete(parsed);
    assert(strstr(response, "\"id\":1") != NULL);
    assert(strstr(response, "\"ok\":true") != NULL);
    assert(strstr(response, "aiqa-management") != NULL);
    assert(strstr(response, "authentication_required") != NULL);
    assert(strstr(response, "password") == NULL);
    assert(strstr(response, "session") == NULL);
}

static void run_diagnostics(void)
{
    const char request[] =
        "{\"id\":2,\"method\":\"system.hello\"}";
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    size_t response_length = 0;
    const aiqa_management_public_protocol_ports_t ports = {
        .context = (void *)0x1234,
        .copy_diagnostics = copy_diagnostics,
    };
    s_diagnostics_calls = 0U;
    assert(aiqa_management_protocol_handle_public_request_with_ports(
        (const uint8_t *)request,
        strlen(request),
        &ports,
        response,
        sizeof(response),
        &response_length));
    assert(s_diagnostics_calls == 1U);
    assert(response_length == strlen(response));
    const char *parse_end = NULL;
    cJSON *parsed = cJSON_ParseWithLengthOpts(
        response, response_length, &parse_end, false);
    assert(parsed != NULL);
    assert(parse_end == response + response_length);
    cJSON_Delete(parsed);
    assert(strstr(response, "\"id\":2") != NULL);
    assert(strstr(response, "\"ok\":true") != NULL);
    assert(strstr(response, "\"generation\":17") != NULL);
    assert(strstr(response, "\"runtimeReady\":true") != NULL);
    assert(strstr(response, "\"runtimeStartPhase\":6") != NULL);
    assert(strstr(response, "\"runtimeStartStatus\":0") != NULL);
    assert(strstr(response, "\"boardInitPhase\":19") != NULL);
    assert(strstr(response, "\"state\":\"TRANSCRIBING\"") != NULL);
    assert(strstr(response, "\"error\":\"NONE\"") != NULL);
    assert(strstr(response, "\"phase\":3") != NULL);
    assert(strstr(response, "\"contentLength\":-28679") != NULL);
    assert(strstr(response, "\"socketErrno\"") == NULL);
    assert(strstr(response, "\"uploadWriteCalls\"") == NULL);
    assert(strstr(response, "\"responseComplete\":false") != NULL);
    assert(strstr(response, "transcript") == NULL);
    assert(strstr(response, "answer") == NULL);
    assert(strstr(response, "ssid") == NULL);
    assert(strstr(response, "password") == NULL);
    assert(strstr(response, "secret") == NULL);
    assert(strstr(response, "apiKey") == NULL);

    const char diagnostics_request[] =
        "{\"id\":3,\"method\":\"system.diagnostics\"}";
    assert(aiqa_management_protocol_handle_public_request_with_ports(
        (const uint8_t *)diagnostics_request,
        strlen(diagnostics_request),
        &ports,
        response,
        sizeof(response),
        &response_length));
    assert(strstr(response, "\"id\":3") != NULL);
    assert(strstr(response, "\"snapshotVersion\":2") != NULL);
    assert(strstr(response, "\"socketErrno\":116") != NULL);
    assert(strstr(response, "\"uploadWriteCalls\":43") != NULL);
    assert(strstr(response, "transcript") == NULL);
    assert(strstr(response, "password") == NULL);

    s_diagnostics_calls = 0U;
    const char invalid[] =
        "{\"id\":2,\"method\":\"system.hello\",\"params\":{}}";
    assert(aiqa_management_protocol_handle_public_request_with_ports(
        (const uint8_t *)invalid,
        strlen(invalid),
        &ports,
        response,
        sizeof(response),
        &response_length));
    assert(s_diagnostics_calls == 0U);
    assert(strstr(response, "INVALID_REQUEST") != NULL);
}

static void assert_rejected(const char *request, const char *error_code)
{
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    size_t response_length = 0;
    assert(aiqa_management_protocol_handle_public_request(
        (const uint8_t *)request,
        strlen(request),
        response,
        sizeof(response),
        &response_length));
    assert(strstr(response, "\"ok\":false") != NULL);
    assert(strstr(response, error_code) != NULL);
}

static void run_reject(void)
{
    assert_rejected("{\"id\":0,\"method\":\"system.hello\"}", "INVALID_REQUEST");
    assert_rejected("{\"id\":2,\"method\":\"device.status.get\"}", "AUTHENTICATION_REQUIRED");
    assert_rejected(
        "{\"id\":3,\"method\":\"system.hello\",\"token\":\"forbidden\"}",
        "INVALID_REQUEST");
    assert_rejected("not-json", "INVALID_REQUEST");
    assert_rejected("{}", "INVALID_REQUEST");
    assert_rejected("{\"id\":1.5,\"method\":\"system.hello\"}", "INVALID_REQUEST");
    assert_rejected("{\"id\":4294967296,\"method\":\"system.hello\"}", "INVALID_REQUEST");
    assert_rejected("{\"id\":true,\"method\":\"system.hello\"}", "INVALID_REQUEST");
    assert_rejected("{\"id\":1,\"method\":null}", "INVALID_REQUEST");
    assert_rejected(
        "{\"id\":1,\"id\":2,\"method\":\"system.hello\"}",
        "INVALID_REQUEST");
}

static void run_nul(void)
{
    const uint8_t request[] = {'{', '}', 0, '{', '}'};
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    size_t response_length = 0;
    assert(aiqa_management_protocol_handle_public_request(
        request, sizeof(request), response, sizeof(response), &response_length));
    assert(strstr(response, "INVALID_REQUEST") != NULL);
    assert_rejected(
        "{\"id\":1,\"method\":\"system.hello\\u0000evil\"}",
        "INVALID_REQUEST");
    assert_rejected(
        "{\"id\\u0000evil\":1,\"method\":\"system.hello\"}",
        "INVALID_REQUEST");
    assert_rejected(
        "{\"id\":1,\"method\":\"system.hello\\u000Gevil\"}",
        "INVALID_REQUEST");
    assert_rejected(
        "{\"id\\uZZZZevil\":1,\"method\":\"system.hello\"}",
        "INVALID_REQUEST");
    assert_rejected(
        "{\"i\\d\":1,\"method\":\"system.hello\"}",
        "INVALID_REQUEST");
}

static void run_depth(void)
{
    assert_rejected(
        "{\"id\":1,\"method\":\"system.hello\",\"x\":"
        "[[[[[[[[[]]]]]]]]]}",
        "INVALID_REQUEST");
}

static void run_boundaries(void)
{
    const char request[] = "{\"method\":\"system.hello\",\"id\":1} \n";
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    size_t response_length = 99;
    assert(aiqa_management_protocol_handle_public_request(
        (const uint8_t *)request,
        strlen(request),
        response,
        sizeof(response),
        &response_length));
    assert(strstr(response, "\"ok\":true") != NULL);

    char too_small[8] = {0};
    response_length = 99;
    assert(!aiqa_management_protocol_handle_public_request(
        (const uint8_t *)request,
        strlen(request),
        too_small,
        sizeof(too_small),
        &response_length));
    assert(response_length == 0);
    assert(too_small[0] == '\0');

    assert(!aiqa_management_protocol_handle_public_request(
        NULL, 0, response, sizeof(response), &response_length));
    assert(!aiqa_management_protocol_handle_public_request(
        (const uint8_t *)request,
        strlen(request),
        NULL,
        sizeof(response),
        &response_length));
    assert(!aiqa_management_protocol_handle_public_request(
        (const uint8_t *)request, strlen(request), response, 0, &response_length));
    assert(!aiqa_management_protocol_handle_public_request(
        (const uint8_t *)request, strlen(request), response, sizeof(response), NULL));

    uint8_t oversized[AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD + 1U];
    (void)memset(oversized, ' ', sizeof(oversized));
    assert(aiqa_management_protocol_handle_public_request(
        oversized,
        sizeof(oversized),
        response,
        sizeof(response),
        &response_length));
    assert(strstr(response, "INVALID_REQUEST") != NULL);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "hello") == 0) {
        run_hello();
    } else if (strcmp(argv[1], "diagnostics") == 0) {
        run_diagnostics();
    } else if (strcmp(argv[1], "reject") == 0) {
        run_reject();
    } else if (strcmp(argv[1], "nul") == 0) {
        run_nul();
    } else if (strcmp(argv[1], "boundaries") == 0) {
        run_boundaries();
    } else if (strcmp(argv[1], "depth") == 0) {
        run_depth();
    } else {
        return 2;
    }
    return 0;
}
