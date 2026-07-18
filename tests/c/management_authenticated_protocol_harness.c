#include "aiqa_management_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    aiqa_management_security_context_t expected_access;
    unsigned status_calls;
    unsigned config_calls;
    unsigned wifi_calls;
    aiqa_management_owned_wifi_update_t last_wifi;
} fixture_t;

static aiqa_management_result_t get_status(
    void *context,
    const aiqa_management_security_context_t *access,
    aiqa_management_device_status_t *out_status)
{
    fixture_t *fixture = context;
    assert(memcmp(access, &fixture->expected_access, sizeof(*access)) == 0);
    fixture->status_calls += 1U;
    *out_status = (aiqa_management_device_status_t){
        .state = AIQA_STATE_IDLE,
        .error = AIQA_ERROR_NONE,
        .sequence = 7,
        .uptime_ms = 123456,
        .free_heap_bytes = 65432,
        .wifi = {.connected = true, .rssi_available = true, .rssi_dbm = -48},
        .power = {
            .battery_present = true,
            .percent_available = true,
            .percent = 82,
            .charging_state = AIQA_MANAGEMENT_CHARGING_DISCHARGING,
        },
        .config = {
            .available = true,
            .revision = 9,
            .has_chat_api_key = true,
            .has_asr_api_key = false,
        },
        .latest_operation = {
            .id = 12,
            .state = AIQA_MANAGEMENT_OPERATION_PENDING,
            .result = AIQA_MANAGEMENT_OK,
        },
    };
    (void)snprintf(out_status->ui.status, sizeof(out_status->ui.status), "READY");
    (void)snprintf(out_status->ui.detail, sizeof(out_status->ui.detail), "device ready");
    (void)snprintf(out_status->ui.hint, sizeof(out_status->ui.hint), "HOLD BOOT");
    (void)snprintf(out_status->ui.expression, sizeof(out_status->ui.expression), "IDLE");
    (void)snprintf(
        out_status->config.chat_provider,
        sizeof(out_status->config.chat_provider),
        "dashscope_openai_chat");
    (void)snprintf(
        out_status->config.chat_model,
        sizeof(out_status->config.chat_model),
        "qwen3.7-max");
    return AIQA_MANAGEMENT_OK;
}

static aiqa_management_result_t get_config(
    void *context,
    const aiqa_management_security_context_t *access,
    aiqa_management_public_config_t *out_config)
{
    fixture_t *fixture = context;
    assert(memcmp(access, &fixture->expected_access, sizeof(*access)) == 0);
    fixture->config_calls += 1U;
    *out_config = (aiqa_management_public_config_t){
        .revision = 9,
        .wifi = {.has_password = true},
        .stream = true,
        .hide_reasoning = true,
        .max_completion_tokens = 256,
        .has_chat_api_key = true,
        .has_asr_api_key = false,
    };
    (void)snprintf(out_config->wifi.ssid, sizeof(out_config->wifi.ssid), "Lab\"Wifi");
    (void)snprintf(out_config->chat_provider, sizeof(out_config->chat_provider), "dashscope_openai_chat");
    (void)snprintf(out_config->chat_model, sizeof(out_config->chat_model), "qwen3.7-max");
    (void)snprintf(out_config->asr_provider, sizeof(out_config->asr_provider), "qwen_asr");
    (void)snprintf(out_config->asr_model, sizeof(out_config->asr_model), "qwen3-asr-flash");
    (void)snprintf(out_config->tts_provider, sizeof(out_config->tts_provider), "qwen_tts");
    (void)snprintf(out_config->tts_model, sizeof(out_config->tts_model), "qwen3-tts-flash");
    (void)snprintf(out_config->tts_voice, sizeof(out_config->tts_voice), "Cherry");
    return AIQA_MANAGEMENT_OK;
}

static aiqa_management_result_t submit_wifi(
    void *context,
    const aiqa_management_security_context_t *access,
    const aiqa_management_owned_wifi_update_t *update,
    uint32_t *out_operation_id)
{
    fixture_t *fixture = context;
    assert(memcmp(access, &fixture->expected_access, sizeof(*access)) == 0);
    fixture->wifi_calls += 1U;
    fixture->last_wifi = *update;
    *out_operation_id = 77;
    return AIQA_MANAGEMENT_OK;
}

static aiqa_management_protocol_ports_t ports_for(fixture_t *fixture)
{
    return (aiqa_management_protocol_ports_t){
        .context = fixture,
        .get_status = get_status,
        .get_public_config = get_config,
        .submit_wifi_update = submit_wifi,
    };
}

static void handle(
    fixture_t *fixture,
    const char *request,
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE])
{
    const aiqa_management_protocol_ports_t ports = ports_for(fixture);
    size_t response_length = 0;
    assert(aiqa_management_protocol_handle_authenticated_request(
        (const uint8_t *)request,
        strlen(request),
        &fixture->expected_access,
        &ports,
        response,
        AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE,
        &response_length));
    assert(response_length == strlen(response));
}

static void run_reads(void)
{
    fixture_t fixture = {
        .expected_access = {
            .connection_generation = 2,
            .authorization_generation = 0x12345678U,
            .session_id = {1},
        },
    };
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    handle(&fixture, "{\"id\":2,\"method\":\"device.status.get\"}", response);
    assert(strstr(response, "\"ok\":true") != NULL);
    assert(strstr(response, "\"state\":\"IDLE\"") != NULL);
    assert(strstr(response, "\"rssiDbm\":-48") != NULL);
    assert(strstr(response, "\"latestOperation\":{") != NULL);
    assert(strstr(response, "apiKey") == NULL);
    assert(fixture.status_calls == 1U);

    (void)memset(response, 0, sizeof(response));
    handle(&fixture, "{\"id\":3,\"method\":\"config.public.get\"}", response);
    assert(strstr(response, "Lab\\\"Wifi") != NULL);
    assert(strstr(response, "\"hasPassword\":true") != NULL);
    assert(strstr(response, "\"hasChatApiKey\":true") != NULL);
    assert(strstr(response, "password\"") == NULL);
    assert(strstr(response, "baseUrl") == NULL);
    assert(fixture.config_calls == 1U);
}

static void run_wifi(void)
{
    fixture_t fixture = {
        .expected_access = {
            .connection_generation = 3,
            .authorization_generation = 7,
            .session_id = {2},
        },
    };
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    handle(
        &fixture,
        "{\"id\":4,\"method\":\"wifi.update\",\"params\":{"
        "\"baseRevision\":9,\"ssid\":\"Office\","
        "\"passwordAction\":\"replace\",\"password\":\"secret123\"}}",
        response);
    assert(strstr(response, "\"operationId\":77") != NULL);
    assert(fixture.wifi_calls == 1U);
    assert(fixture.last_wifi.base_revision == 9U);
    assert(fixture.last_wifi.password_action == AIQA_WIFI_PASSWORD_REPLACE);
    assert(strcmp(fixture.last_wifi.ssid, "Office") == 0);
    assert(strcmp(fixture.last_wifi.password, "secret123") == 0);

    (void)memset(response, 0, sizeof(response));
    handle(
        &fixture,
        "{\"id\":5,\"method\":\"wifi.update\",\"params\":{"
        "\"baseRevision\":9,\"ssid\":\"Office\","
        "\"passwordAction\":\"keep\",\"password\":\"must-reject\"}}",
        response);
    assert(strstr(response, "INVALID_REQUEST") != NULL);
    assert(fixture.wifi_calls == 1U);

    (void)memset(response, 0, sizeof(response));
    handle(
        &fixture,
        "{\"id\":6,\"method\":\"wifi.update\",\"params\":{"
        "\"baseRevision\":9,\"ssid\":\"Bad\\u0001Name\","
        "\"passwordAction\":\"keep\"}}",
        response);
    assert(strstr(response, "INVALID_REQUEST") != NULL);
    assert(fixture.wifi_calls == 1U);
}

static void run_reject(void)
{
    fixture_t fixture = {
        .expected_access = {
            .connection_generation = 4,
            .authorization_generation = 11,
            .session_id = {3},
        },
    };
    char response[AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    handle(&fixture, "{\"id\":1,\"method\":\"system.hello\"}", response);
    assert(strstr(response, "METHOD_NOT_FOUND") != NULL);

    (void)memset(response, 0, sizeof(response));
    handle(&fixture, "{\"id\":1,\"method\":\"device.status.get\",\"params\":{}}", response);
    assert(strstr(response, "INVALID_REQUEST") != NULL);

    const aiqa_management_protocol_ports_t ports = ports_for(&fixture);
    size_t length = 99;
    assert(!aiqa_management_protocol_handle_authenticated_request(
        (const uint8_t *)"{}", 2, NULL, &ports, response, sizeof(response), &length));
    assert(length == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "reads") == 0) {
        run_reads();
    } else if (strcmp(argv[1], "wifi") == 0) {
        run_wifi();
    } else if (strcmp(argv[1], "reject") == 0) {
        run_reject();
    } else {
        return 2;
    }
    return 0;
}
