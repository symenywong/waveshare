#include "aiqa_management_protocol.h"
#include "aiqa_management_wire.h"

#include <assert.h>
#include <string.h>

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
    assert(strstr(response, "\"id\":1") != NULL);
    assert(strstr(response, "\"ok\":true") != NULL);
    assert(strstr(response, "aiqa-management") != NULL);
    assert(strstr(response, "authentication_required") != NULL);
    assert(strstr(response, "password") == NULL);
    assert(strstr(response, "session") == NULL);
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
