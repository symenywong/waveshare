#include "aiqa_chat_intent_protocol.h"
#include "aiqa_device_intent_controller.h"

#include "cJSON.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *valid_response(const char *arguments)
{
    static char response[1024];
    const int written = snprintf(
        response,
        sizeof(response),
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{"
        "\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{"
        "\"id\":\"call_1\",\"type\":\"function\",\"function\":{"
        "\"name\":\"route_device_intent\",\"arguments\":\"%s\"}}]}}]}",
        arguments);
    assert(written > 0 && (size_t)written < sizeof(response));
    return response;
}

static const char *valid_response_without_content(const char *arguments)
{
    static char response[1024];
    const int written = snprintf(
        response,
        sizeof(response),
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{"
        "\"role\":\"assistant\",\"tool_calls\":[{"
        "\"id\":\"call_1\",\"type\":\"function\",\"function\":{"
        "\"name\":\"route_device_intent\",\"arguments\":\"%s\"}}]}}]}",
        arguments);
    assert(written > 0 && (size_t)written < sizeof(response));
    return response;
}

static void test_request(void)
{
    aiqa_config_t config = aiqa_config_default();
    char request[AIQA_CHAT_REQUEST_MAX_LEN] = {0};
    assert(aiqa_chat_build_intent_request_json(
               &config,
               "你的名字叫闪电, 知道了吗",
               request,
               sizeof(request)) == AIQA_CHAT_OK);

    cJSON *root = cJSON_Parse(request);
    assert(root != NULL);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "stream")));
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "enable_thinking")));
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "parallel_tool_calls")));
    const cJSON *messages = cJSON_GetObjectItemCaseSensitive(root, "messages");
    assert(cJSON_IsArray(messages) && cJSON_GetArraySize(messages) == 2);
    const cJSON *tools = cJSON_GetObjectItemCaseSensitive(root, "tools");
    assert(cJSON_IsArray(tools) && cJSON_GetArraySize(tools) == 1);
    const cJSON *tool = cJSON_GetArrayItem(tools, 0);
    const cJSON *function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    assert(strcmp(cJSON_GetStringValue(
                      cJSON_GetObjectItemCaseSensitive(function, "name")),
                  "route_device_intent") == 0);
    const cJSON *parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(parameters, "additionalProperties")));
    const cJSON *properties = cJSON_GetObjectItemCaseSensitive(parameters, "properties");
    const cJSON *schema_version = cJSON_GetObjectItemCaseSensitive(properties, "schema_version");
    const cJSON *version_const = cJSON_GetObjectItemCaseSensitive(schema_version, "const");
    assert(cJSON_IsNumber(version_const) && version_const->valueint == 1);
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(properties, "action");
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(properties, "value");
    const cJSON *evidence = cJSON_GetObjectItemCaseSensitive(properties, "evidence");
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(action, "description")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(value, "description")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(evidence, "description")));
    const cJSON *required = cJSON_GetObjectItemCaseSensitive(parameters, "required");
    assert(cJSON_IsArray(required) && cJSON_GetArraySize(required) == 4);
    const cJSON *tool_choice = cJSON_GetObjectItemCaseSensitive(root, "tool_choice");
    assert(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
                      cJSON_GetObjectItemCaseSensitive(tool_choice, "function"), "name")),
                  "route_device_intent") == 0);
    assert(strstr(request, "闪电") != NULL);
    assert(strstr(request, "assistant_profile.name") != NULL);
    assert(strstr(request, "persistent local device property") != NULL);
    assert(strstr(request, "classification proposal") != NULL);
    assert(strstr(request, "Recent conversation memory") == NULL);
    assert(strstr(request, "Assistant profile") == NULL);
    assert(strstr(request, "sk-") == NULL);
    cJSON_Delete(root);
}

static void assert_parse(
    const char *arguments,
    const char *transcript,
    aiqa_device_intent_type_t expected,
    const char *expected_value)
{
    const char *response = valid_response(arguments);
    aiqa_device_intent_t intent = {0};
    assert(aiqa_chat_parse_intent_response_json(
               response, strlen(response), transcript, &intent) == AIQA_CHAT_OK);
    assert(intent.type == expected);
    assert(strcmp(intent.value, expected_value) == 0);
}

static void test_valid_responses(void)
{
    assert_parse(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_name\\\","
        "\\\"value\\\":\\\"闪电\\\",\\\"evidence\\\":\\\"闪电\\\"}",
        "你的名字叫闪电, 知道了吗",
        AIQA_DEVICE_INTENT_SET_NAME,
        "闪电");
    assert_parse(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_gender\\\","
        "\\\"value\\\":\\\"female\\\",\\\"evidence\\\":\\\"女性\\\"}",
        "以后你是女性形象",
        AIQA_DEVICE_INTENT_SET_GENDER,
        "female");
    assert_parse(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_language\\\","
        "\\\"value\\\":\\\"en\\\",\\\"evidence\\\":\\\"英语\\\"}",
        "接下来使用英语",
        AIQA_DEVICE_INTENT_SET_LANGUAGE,
        "en");
    assert_parse(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"get_profile\\\","
        "\\\"value\\\":\\\"name\\\",\\\"evidence\\\":\\\"叫什么名字\\\"}",
        "你叫什么名字",
        AIQA_DEVICE_INTENT_GET_PROFILE_NAME,
        "");
    assert_parse(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"none\\\","
        "\\\"value\\\":\\\"\\\",\\\"evidence\\\":\\\"\\\"}",
        "讲个故事",
        AIQA_DEVICE_INTENT_NONE,
        "");

    const char *without_content = valid_response_without_content(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_name\\\","
        "\\\"value\\\":\\\"闪电\\\",\\\"evidence\\\":\\\"闪电\\\"}");
    aiqa_device_intent_t intent = {0};
    assert(aiqa_chat_parse_intent_response_json(
               without_content,
               strlen(without_content),
               "你的名字叫闪电",
               &intent) == AIQA_CHAT_OK);

    char with_trailing_whitespace[1100] = {0};
    const char *base = valid_response(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"none\\\","
        "\\\"value\\\":\\\"\\\",\\\"evidence\\\":\\\"\\\"}");
    assert(snprintf(with_trailing_whitespace,
                    sizeof(with_trailing_whitespace),
                    "%s \r\n\t",
                    base) > 0);
    assert(aiqa_chat_parse_intent_response_json(
               with_trailing_whitespace,
               strlen(with_trailing_whitespace),
               "讲个故事",
               &intent) == AIQA_CHAT_OK);

    const char *official_shape =
        "{\"id\":\"chatcmpl-test\",\"model\":\"qwen3.7-max\",\"choices\":[{"
        "\"index\":0,\"finish_reason\":\"tool_calls\",\"logprobs\":null,\"message\":{"
        "\"role\":\"assistant\",\"content\":\"\",\"reasoning_content\":\"\","
        "\"tool_calls\":[{\"id\":\"call_1\",\"type\":\"function\",\"function\":{"
        "\"name\":\"route_device_intent\",\"arguments\":"
        "\"{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_name\\\","
        "\\\"value\\\":\\\"闪电\\\",\\\"evidence\\\":\\\"闪电\\\"}\"}}]}}],"
        "\"usage\":{\"prompt_tokens\":100,\"completion_tokens\":20,\"total_tokens\":120}}";
    assert(aiqa_chat_parse_intent_response_json(
               official_shape,
               strlen(official_shape),
               "你的名字叫闪电",
               &intent) == AIQA_CHAT_OK);

    const char *live_qwen_shape =
        "{\"choices\":[{\"finish_reason\":\"stop\",\"index\":0,\"message\":{"
        "\"content\":\"\",\"role\":\"assistant\",\"tool_calls\":[{\"function\":{"
        "\"arguments\":\"{\\\"schema_version\\\": 1, \\\"action\\\": \\\"set_name\\\", "
        "\\\"value\\\": \\\"闪电\\\", \\\"evidence\\\": \\\"你的名字叫闪电，知道了吗\\\"}\","
        "\"name\":\"route_device_intent\"},\"id\":\"call_live\",\"index\":0,"
        "\"type\":\"function\"}]}}],\"model\":\"qwen3.7-max\","
        "\"object\":\"chat.completion\"}";
    assert(aiqa_chat_parse_intent_response_json(
               live_qwen_shape,
               strlen(live_qwen_shape),
               "你的名字叫闪电，知道了吗",
               &intent) == AIQA_CHAT_OK);
}

static void assert_rejected(const char *response, const char *transcript)
{
    aiqa_device_intent_t intent = {
        .type = AIQA_DEVICE_INTENT_SET_NAME,
        .value = "should-clear",
    };
    assert(aiqa_chat_parse_intent_response_json(
               response, strlen(response), transcript, &intent) == AIQA_CHAT_ERR_PARSE);
    assert(intent.type == AIQA_DEVICE_INTENT_INVALID);
    assert(intent.value[0] == '\0');
}

static void test_rejections(void)
{
    assert_rejected(
        valid_response("{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_name\\\","
                       "\\\"value\\\":\\\"闪电\\\",\\\"extra\\\":1,"
                       "\\\"evidence\\\":\\\"闪电\\\"}"),
        "你的名字叫闪电");
    assert_rejected(
        valid_response("{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_gender\\\","
                       "\\\"value\\\":\\\"male\\\",\\\"evidence\\\":\\\"女性\\\"}"),
        "以后改成女性形象");
    assert_rejected(
        valid_response("{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_language\\\","
                       "\\\"value\\\":\\\"en\\\",\\\"evidence\\\":\\\"中文\\\"}"),
        "以后使用中文");
    assert_rejected(
        valid_response("{\\\"schema_version\\\":1,\\\"action\\\":\\\"get_profile\\\","
                       "\\\"value\\\":\\\"name\\\",\\\"evidence\\\":\\\"语言\\\"}"),
        "当前对话语言是什么");
    assert_rejected(
        valid_response("{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_name\\\","
                       "\\\"value\\\":\\\"闪电\\\",\\\"value\\\":\\\"AIQA\\\","
                       "\\\"evidence\\\":\\\"闪电\\\"}"),
        "你的名字叫闪电");
    assert_rejected(
        valid_response("{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_name\\\","
                       "\\\"value\\\":\\\"闪电\\\",\\\"evidence\\\":\\\"不存在\\\"}"),
        "你的名字叫闪电");
    assert_rejected(
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{"
        "\"content\":\"{\\\"tool_calls\\\":[]}\"}}]}",
        "你的名字叫闪电");
    assert_rejected(
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{"
        "\"content\":null,\"tool_calls\":[]}}]}",
        "你的名字叫闪电");
}

typedef struct {
    aiqa_assistant_profile_t durable_profile;
    aiqa_assistant_profile_t published_profile;
    unsigned save_count;
    unsigned publish_count;
    char order[4];
    size_t order_length;
} journey_store_t;

static bool journey_load_profile(void *context, aiqa_assistant_profile_t *out_profile)
{
    *out_profile = ((journey_store_t *)context)->published_profile;
    return true;
}

static bool journey_load_language(void *context, aiqa_dialogue_language_t *out_language)
{
    (void)context;
    *out_language = AIQA_DIALOGUE_LANGUAGE_CHINESE;
    return true;
}

static esp_err_t journey_save_profile(
    void *context,
    const aiqa_assistant_profile_t *profile)
{
    journey_store_t *store = context;
    store->durable_profile = *profile;
    store->save_count += 1U;
    store->order[store->order_length++] = 'S';
    return ESP_OK;
}

static void journey_publish_profile(
    void *context,
    const aiqa_assistant_profile_t *profile)
{
    journey_store_t *store = context;
    store->published_profile = *profile;
    store->publish_count += 1U;
    store->order[store->order_length++] = 'P';
}

static esp_err_t journey_save_language(void *context, aiqa_dialogue_language_t language)
{
    (void)context;
    (void)language;
    return ESP_OK;
}

static void journey_publish_language(void *context, aiqa_dialogue_language_t language)
{
    (void)context;
    (void)language;
}

static bool journey_authorize(void *context, uint32_t generation)
{
    (void)context;
    return generation != 0U;
}

static void test_full_name_journey(void)
{
    const char *proposal_transcript = "你的名字叫闪电，知道了吗";
    const char *proposal_response = valid_response(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"set_name\\\","
        "\\\"value\\\":\\\"闪电\\\",\\\"evidence\\\":\\\"闪电\\\"}");
    aiqa_device_intent_t proposal = {0};
    assert(aiqa_chat_parse_intent_response_json(
               proposal_response,
               strlen(proposal_response),
               proposal_transcript,
               &proposal) == AIQA_CHAT_OK);

    journey_store_t store = {
        .durable_profile = aiqa_assistant_profile_default(),
        .published_profile = aiqa_assistant_profile_default(),
    };
    const aiqa_device_intent_ports_t ports = {
        .context = &store,
        .load_profile = journey_load_profile,
        .load_language = journey_load_language,
        .save_profile = journey_save_profile,
        .publish_profile = journey_publish_profile,
        .save_language = journey_save_language,
        .publish_language = journey_publish_language,
        .authorize_generation = journey_authorize,
    };
    aiqa_device_intent_controller_t controller;
    aiqa_device_intent_controller_init(&controller);
    char reply[AIQA_DEVICE_INTENT_REPLY_MAX_LEN] = {0};
    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller,
        &proposal,
        proposal_transcript,
        100U,
        1000000,
        &ports,
        reply,
        sizeof(reply)));
    assert(store.save_count == 0U && store.publish_count == 0U);
    assert(strcmp(store.durable_profile.name, "AIQA") == 0);
    assert(strstr(reply, "确认") != NULL);

    assert(aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "确认", 101U, 2000000, &ports, reply, sizeof(reply)));
    assert(store.save_count == 1U && store.publish_count == 1U);
    assert(store.order[0] == 'S' && store.order[1] == 'P');
    assert(strcmp(store.durable_profile.name, "闪电") == 0);

    aiqa_device_intent_controller_init(&controller);
    store.published_profile = store.durable_profile;
    const char *query_transcript = "你叫什么名字";
    const char *query_response = valid_response(
        "{\\\"schema_version\\\":1,\\\"action\\\":\\\"get_profile\\\","
        "\\\"value\\\":\\\"name\\\",\\\"evidence\\\":\\\"叫什么名字\\\"}");
    aiqa_device_intent_t query = {0};
    assert(aiqa_chat_parse_intent_response_json(
               query_response, strlen(query_response), query_transcript, &query) == AIQA_CHAT_OK);
    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller,
        &query,
        query_transcript,
        102U,
        3000000,
        &ports,
        reply,
        sizeof(reply)));
    assert(strcmp(reply, "我的名字是闪电。") == 0);
    assert(store.save_count == 1U);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--print-live-request") == 0) {
        aiqa_config_t config = aiqa_config_default();
        char request[AIQA_CHAT_REQUEST_MAX_LEN] = {0};
        assert(aiqa_chat_build_intent_request_json(
                   &config,
                   "你的名字叫闪电，知道了吗",
                   request,
                   sizeof(request)) == AIQA_CHAT_OK);
        puts(request);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--parse-live-response") == 0) {
        char response[4097] = {0};
        const size_t length = fread(response, 1U, sizeof(response) - 1U, stdin);
        aiqa_device_intent_t intent = {0};
        const aiqa_chat_status_t status = aiqa_chat_parse_intent_response_json(
            response,
            length,
            "你的名字叫闪电，知道了吗",
            &intent);
        printf("status=%d type=%d value=%s evidence=%s\n",
               (int)status,
               (int)intent.type,
               intent.value,
               intent.evidence);
        return status == AIQA_CHAT_OK ? 0 : 1;
    }
    test_request();
    test_valid_responses();
    test_rejections();
    test_full_name_journey();
    return 0;
}
