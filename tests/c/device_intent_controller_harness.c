#include "aiqa_device_intent_controller.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    aiqa_assistant_profile_t profile;
    aiqa_dialogue_language_t language;
    esp_err_t save_result;
    unsigned save_count;
    unsigned publish_count;
    char order[8];
    size_t order_length;
    bool authorized;
} fake_store_t;

static bool load_profile(void *context, aiqa_assistant_profile_t *out_profile)
{
    *out_profile = ((fake_store_t *)context)->profile;
    return true;
}

static bool authorize_generation(void *context, uint32_t generation)
{
    (void)generation;
    return ((fake_store_t *)context)->authorized;
}

static bool load_language(void *context, aiqa_dialogue_language_t *out_language)
{
    *out_language = ((fake_store_t *)context)->language;
    return true;
}

static esp_err_t save_profile(void *context, const aiqa_assistant_profile_t *profile)
{
    (void)profile;
    fake_store_t *store = context;
    store->save_count += 1U;
    store->order[store->order_length++] = 'S';
    return store->save_result;
}

static void publish_profile(void *context, const aiqa_assistant_profile_t *profile)
{
    fake_store_t *store = context;
    store->profile = *profile;
    store->publish_count += 1U;
    store->order[store->order_length++] = 'P';
}

static esp_err_t save_language(void *context, aiqa_dialogue_language_t language)
{
    (void)language;
    fake_store_t *store = context;
    store->save_count += 1U;
    store->order[store->order_length++] = 'S';
    return store->save_result;
}

static void publish_language(void *context, aiqa_dialogue_language_t language)
{
    fake_store_t *store = context;
    store->language = language;
    store->publish_count += 1U;
    store->order[store->order_length++] = 'P';
}

static aiqa_device_intent_ports_t ports_for(fake_store_t *store)
{
    return (aiqa_device_intent_ports_t){
        .context = store,
        .load_profile = load_profile,
        .load_language = load_language,
        .save_profile = save_profile,
        .publish_profile = publish_profile,
        .save_language = save_language,
        .publish_language = publish_language,
        .authorize_generation = authorize_generation,
    };
}

static aiqa_device_intent_t intent(
    aiqa_device_intent_type_t type,
    const char *value,
    const char *evidence)
{
    aiqa_device_intent_t result = {.type = type};
    (void)snprintf(result.value, sizeof(result.value), "%s", value);
    (void)snprintf(result.evidence, sizeof(result.evidence), "%s", evidence);
    return result;
}

static void test_propose_confirm_and_replay(void)
{
    fake_store_t store = {
        .profile = aiqa_assistant_profile_default(),
        .language = AIQA_DIALOGUE_LANGUAGE_CHINESE,
        .save_result = ESP_OK,
        .authorized = true,
    };
    const aiqa_device_intent_ports_t ports = ports_for(&store);
    aiqa_device_intent_controller_t controller;
    aiqa_device_intent_controller_init(&controller);
    char reply[AIQA_DEVICE_INTENT_REPLY_MAX_LEN] = {0};
    const aiqa_device_intent_t set_name = intent(
        AIQA_DEVICE_INTENT_SET_NAME, "闪电", "闪电");

    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller, &set_name, "你的名字叫闪电", 10U, 1000000, &ports, reply, sizeof(reply)));
    assert(aiqa_device_intent_controller_has_pending(&controller));
    assert(store.save_count == 0U && store.publish_count == 0U);
    assert(strcmp(store.profile.name, "AIQA") == 0);
    assert(strstr(reply, "闪电") != NULL && strstr(reply, "确认") != NULL);

    assert(!aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "确认", 10U, 2000000, &ports, reply, sizeof(reply)));
    assert(store.save_count == 0U);
    assert(aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "确认", 11U, 2000000, &ports, reply, sizeof(reply)));
    assert(store.save_count == 1U && store.publish_count == 1U);
    assert(store.order[0] == 'S' && store.order[1] == 'P');
    assert(strcmp(store.profile.name, "闪电") == 0);
    assert(!aiqa_device_intent_controller_has_pending(&controller));

    assert(!aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "确认", 12U, 3000000, &ports, reply, sizeof(reply)));
    assert(store.save_count == 1U);
}

static void test_cancel_expiry_and_unrelated(void)
{
    fake_store_t store = {
        .profile = aiqa_assistant_profile_default(),
        .language = AIQA_DIALOGUE_LANGUAGE_CHINESE,
        .save_result = ESP_OK,
        .authorized = true,
    };
    const aiqa_device_intent_ports_t ports = ports_for(&store);
    aiqa_device_intent_controller_t controller;
    aiqa_device_intent_controller_init(&controller);
    char reply[AIQA_DEVICE_INTENT_REPLY_MAX_LEN] = {0};
    const aiqa_device_intent_t set_name = intent(
        AIQA_DEVICE_INTENT_SET_NAME, "闪电", "闪电");

    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller, &set_name, "你的名字叫闪电", 20U, 1000000, &ports, reply, sizeof(reply)));
    assert(aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "取消", 21U, 2000000, &ports, reply, sizeof(reply)));
    assert(strstr(reply, "取消") != NULL && store.save_count == 0U);

    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller, &set_name, "你的名字叫闪电", 30U, 1000000, &ports, reply, sizeof(reply)));
    assert(!aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "讲个故事", 31U, 2000000, &ports, reply, sizeof(reply)));
    assert(!aiqa_device_intent_controller_has_pending(&controller));

    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller, &set_name, "你的名字叫闪电", 40U, 1000000, &ports, reply, sizeof(reply)));
    assert(!aiqa_device_intent_controller_handle_pending_transcript(
        &controller,
        "确认",
        41U,
        1000000 + AIQA_DEVICE_INTENT_PENDING_TTL_US + 1,
        &ports,
        reply,
        sizeof(reply)));
    assert(!aiqa_device_intent_controller_has_pending(&controller));
}

static void test_save_failure_and_get_profile(void)
{
    fake_store_t store = {
        .profile = aiqa_assistant_profile_default(),
        .language = AIQA_DIALOGUE_LANGUAGE_CHINESE,
        .save_result = ESP_FAIL,
        .authorized = true,
    };
    assert(aiqa_assistant_profile_set_name(&store.profile, "小皮"));
    const aiqa_device_intent_ports_t ports = ports_for(&store);
    aiqa_device_intent_controller_t controller;
    aiqa_device_intent_controller_init(&controller);
    char reply[AIQA_DEVICE_INTENT_REPLY_MAX_LEN] = {0};

    const aiqa_device_intent_t query = intent(
        AIQA_DEVICE_INTENT_GET_PROFILE_NAME, "", "叫什么名字");
    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller, &query, "你叫什么名字", 50U, 1000000, &ports, reply, sizeof(reply)));
    assert(strcmp(reply, "我的名字是小皮。") == 0);
    assert(store.save_count == 0U && store.publish_count == 0U);

    const aiqa_device_intent_t set_language = intent(
        AIQA_DEVICE_INTENT_SET_LANGUAGE, "en", "英语");
    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller, &set_language, "以后使用英语", 60U, 1000000, &ports, reply, sizeof(reply)));
    assert(aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "确认修改", 61U, 2000000, &ports, reply, sizeof(reply)));
    assert(store.save_count == 1U && store.publish_count == 0U);
    assert(store.language == AIQA_DIALOGUE_LANGUAGE_CHINESE);
    assert(strstr(reply, "保存失败") != NULL);
    assert(!aiqa_device_intent_controller_has_pending(&controller));
}

static void test_stale_generation_cannot_persist(void)
{
    fake_store_t store = {
        .profile = aiqa_assistant_profile_default(),
        .language = AIQA_DIALOGUE_LANGUAGE_CHINESE,
        .save_result = ESP_OK,
        .authorized = true,
    };
    const aiqa_device_intent_ports_t ports = ports_for(&store);
    aiqa_device_intent_controller_t controller;
    aiqa_device_intent_controller_init(&controller);
    char reply[AIQA_DEVICE_INTENT_REPLY_MAX_LEN] = {0};
    const aiqa_device_intent_t set_name = intent(
        AIQA_DEVICE_INTENT_SET_NAME, "闪电", "闪电");

    assert(aiqa_device_intent_controller_handle_cloud_intent(
        &controller, &set_name, "你的名字叫闪电", 70U, 1000000, &ports, reply, sizeof(reply)));
    store.authorized = false;
    assert(aiqa_device_intent_controller_handle_pending_transcript(
        &controller, "确认", 71U, 2000000, &ports, reply, sizeof(reply)));
    assert(store.save_count == 0U && store.publish_count == 0U);
    assert(strcmp(store.profile.name, "AIQA") == 0);
    assert(strstr(reply, "失效") != NULL);
}

int main(void)
{
    test_propose_confirm_and_replay();
    test_cancel_expiry_and_unrelated();
    test_save_failure_and_get_profile();
    test_stale_generation_cannot_persist();
    return 0;
}
