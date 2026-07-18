#include "aiqa_device_intent_controller.h"

#include <stdio.h>
#include <string.h>

static bool ports_are_valid(const aiqa_device_intent_ports_t *ports)
{
    return ports != NULL && ports->load_profile != NULL && ports->load_language != NULL &&
           ports->save_profile != NULL && ports->publish_profile != NULL &&
           ports->save_language != NULL && ports->publish_language != NULL &&
           ports->authorize_generation != NULL;
}

static void set_reply(char *out_reply, size_t capacity, const char *format, const char *value)
{
    if (out_reply == NULL || capacity == 0U) {
        return;
    }
    if (value == NULL) {
        (void)snprintf(out_reply, capacity, "%s", format);
    } else {
        (void)snprintf(out_reply, capacity, format, value);
    }
}

static uint32_t next_generation(uint32_t generation)
{
    return generation == UINT32_MAX ? 1U : generation + 1U;
}

static void clear_pending(aiqa_device_intent_pending_t *pending)
{
    if (pending == NULL) {
        return;
    }
    aiqa_device_intent_clear(&pending->intent);
    *pending = (aiqa_device_intent_pending_t){0};
}

void aiqa_device_intent_controller_init(aiqa_device_intent_controller_t *controller)
{
    if (controller != NULL) {
        *controller = (aiqa_device_intent_controller_t){.next_serial = 1U};
    }
}

void aiqa_device_intent_controller_clear(aiqa_device_intent_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    clear_pending(&controller->pending);
    controller->next_serial = 1U;
}

bool aiqa_device_intent_controller_has_pending(
    const aiqa_device_intent_controller_t *controller)
{
    return controller != NULL && controller->pending.active;
}

static bool offer_setting(
    aiqa_device_intent_controller_t *controller,
    const aiqa_device_intent_t *intent,
    uint32_t generation,
    int64_t now_us,
    const aiqa_device_intent_ports_t *ports,
    char *out_reply,
    size_t reply_capacity)
{
    if (intent->type == AIQA_DEVICE_INTENT_SET_NAME) {
        aiqa_assistant_profile_t profile = {0};
        if (!ports->load_profile(ports->context, &profile)) {
            set_reply(out_reply, reply_capacity, "配置尚未就绪。", NULL);
            return true;
        }
        if (strcmp(profile.name, intent->value) == 0) {
            clear_pending(&controller->pending);
            set_reply(out_reply, reply_capacity, "我的名字已经是%s。", profile.name);
            return true;
        }
        if (!aiqa_assistant_profile_set_name(&profile, intent->value)) {
            return false;
        }
    } else if (intent->type == AIQA_DEVICE_INTENT_SET_GENDER) {
        if (strcmp(intent->value, "neutral") != 0 && strcmp(intent->value, "female") != 0 &&
            strcmp(intent->value, "male") != 0) {
            return false;
        }
    } else if (intent->type == AIQA_DEVICE_INTENT_SET_LANGUAGE) {
        aiqa_dialogue_language_t language = aiqa_language_default();
        if (!aiqa_language_from_chat_code(intent->value, &language)) {
            return false;
        }
    } else {
        return false;
    }
    clear_pending(&controller->pending);
    uint32_t serial = controller->next_serial++;
    if (serial == 0U) {
        serial = controller->next_serial++;
    }
    controller->pending = (aiqa_device_intent_pending_t){
        .active = true,
        .serial = serial,
        .proposal_generation = generation,
        .confirmation_generation = next_generation(generation),
        .expires_at_us = now_us + AIQA_DEVICE_INTENT_PENDING_TTL_US,
        .intent = *intent,
    };
    switch (intent->type) {
    case AIQA_DEVICE_INTENT_SET_NAME:
        set_reply(out_reply, reply_capacity,
                  "我理解为把名字改成%s。请下一次只说确认或取消。", intent->value);
        return true;
    case AIQA_DEVICE_INTENT_SET_GENDER:
        set_reply(out_reply, reply_capacity,
                  "我理解为把形象性别改成%s。请下一次只说确认或取消。", intent->value);
        return true;
    case AIQA_DEVICE_INTENT_SET_LANGUAGE:
        set_reply(out_reply, reply_capacity,
                  "我理解为把对话语言改成%s。请下一次只说确认或取消。", intent->value);
        return true;
    default:
        clear_pending(&controller->pending);
        return false;
    }
}

static bool answer_profile_query(
    const aiqa_device_intent_t *intent,
    const aiqa_device_intent_ports_t *ports,
    char *out_reply,
    size_t reply_capacity)
{
    if (intent->type == AIQA_DEVICE_INTENT_GET_PROFILE_LANGUAGE) {
        aiqa_dialogue_language_t language = aiqa_language_default();
        if (!ports->load_language(ports->context, &language)) {
            set_reply(out_reply, reply_capacity, "配置尚未就绪。", NULL);
            return true;
        }
        set_reply(out_reply, reply_capacity,
                  language == AIQA_DIALOGUE_LANGUAGE_ENGLISH
                      ? "当前对话语言是英语。"
                      : "当前对话语言是中文。",
                  NULL);
        return true;
    }
    aiqa_assistant_profile_t profile = {0};
    if (!ports->load_profile(ports->context, &profile)) {
        set_reply(out_reply, reply_capacity, "配置尚未就绪。", NULL);
        return true;
    }
    if (intent->type == AIQA_DEVICE_INTENT_GET_PROFILE_NAME) {
        set_reply(out_reply, reply_capacity, "我的名字是%s。", profile.name);
        return true;
    }
    if (intent->type == AIQA_DEVICE_INTENT_GET_PROFILE_GENDER) {
        set_reply(out_reply, reply_capacity, "我的形象性别是%s。",
                  aiqa_assistant_gender_name(profile.gender));
        return true;
    }
    return false;
}

bool aiqa_device_intent_controller_handle_cloud_intent(
    aiqa_device_intent_controller_t *controller,
    const aiqa_device_intent_t *intent,
    const char *transcript,
    uint32_t generation,
    int64_t now_us,
    const aiqa_device_intent_ports_t *ports,
    char *out_reply,
    size_t reply_capacity)
{
    if (out_reply != NULL && reply_capacity > 0U) {
        out_reply[0] = '\0';
    }
    if (controller == NULL || intent == NULL || transcript == NULL || generation == 0U ||
        !ports_are_valid(ports) || out_reply == NULL || reply_capacity == 0U) {
        return false;
    }
    if (!aiqa_device_intent_is_valid(intent, transcript)) {
        return false;
    }
    if (aiqa_device_intent_is_setting(intent->type)) {
        return offer_setting(
            controller, intent, generation, now_us, ports, out_reply, reply_capacity);
    }
    if (intent->type == AIQA_DEVICE_INTENT_GET_PROFILE_NAME ||
        intent->type == AIQA_DEVICE_INTENT_GET_PROFILE_GENDER ||
        intent->type == AIQA_DEVICE_INTENT_GET_PROFILE_LANGUAGE) {
        clear_pending(&controller->pending);
        return answer_profile_query(intent, ports, out_reply, reply_capacity);
    }
    return false;
}

static bool normalized_equals(const char *transcript, const char *expected)
{
    while (*transcript == ' ' || *transcript == '\t') {
        transcript += 1;
    }
    size_t length = strlen(transcript);
    while (length > 0U &&
           (transcript[length - 1U] == ' ' || transcript[length - 1U] == '\t' ||
            transcript[length - 1U] == '.' || transcript[length - 1U] == '!')) {
        length -= 1U;
    }
    while (length >= 3U &&
           (memcmp(transcript + length - 3U, "。", 3U) == 0 ||
            memcmp(transcript + length - 3U, "！", 3U) == 0)) {
        length -= 3U;
    }
    return length == strlen(expected) && memcmp(transcript, expected, length) == 0;
}

static bool is_confirmation(const char *transcript)
{
    return normalized_equals(transcript, "确认") ||
           normalized_equals(transcript, "确认修改") ||
           normalized_equals(transcript, "confirm");
}

static bool is_cancellation(const char *transcript)
{
    return normalized_equals(transcript, "取消") ||
           normalized_equals(transcript, "不要修改") ||
           normalized_equals(transcript, "cancel");
}

static bool apply_pending_profile(
    const aiqa_device_intent_t *intent,
    const aiqa_device_intent_ports_t *ports,
    uint32_t generation,
    char *out_reply,
    size_t reply_capacity)
{
    if (!ports->authorize_generation(ports->context, generation)) {
        set_reply(out_reply, reply_capacity, "这次设置已失效，请重新说。", NULL);
        return true;
    }
    aiqa_assistant_profile_t profile = {0};
    if (!ports->load_profile(ports->context, &profile)) {
        set_reply(out_reply, reply_capacity, "配置尚未就绪。", NULL);
        return true;
    }
    bool updated = false;
    if (intent->type == AIQA_DEVICE_INTENT_SET_NAME) {
        updated = aiqa_assistant_profile_set_name(&profile, intent->value);
    } else if (intent->type == AIQA_DEVICE_INTENT_SET_GENDER) {
        updated = aiqa_assistant_profile_set_gender(
            &profile, aiqa_assistant_gender_from_name(intent->value));
    }
    if (!updated || ports->save_profile(ports->context, &profile) != ESP_OK) {
        set_reply(out_reply, reply_capacity, "设置保存失败，请重试。", NULL);
        return true;
    }
    ports->publish_profile(ports->context, &profile);
    if (intent->type == AIQA_DEVICE_INTENT_SET_NAME) {
        set_reply(out_reply, reply_capacity, "好的，我的名字是%s。", profile.name);
    } else {
        set_reply(out_reply, reply_capacity, "好的，形象性别已更新。", NULL);
    }
    return true;
}

static bool apply_pending_language(
    const aiqa_device_intent_t *intent,
    const aiqa_device_intent_ports_t *ports,
    uint32_t generation,
    char *out_reply,
    size_t reply_capacity)
{
    if (!ports->authorize_generation(ports->context, generation)) {
        set_reply(out_reply, reply_capacity, "这次设置已失效，请重新说。", NULL);
        return true;
    }
    aiqa_dialogue_language_t language = aiqa_language_default();
    if (!aiqa_language_from_chat_code(intent->value, &language) ||
        ports->save_language(ports->context, language) != ESP_OK) {
        set_reply(out_reply, reply_capacity, "语言设置保存失败，请重试。", NULL);
        return true;
    }
    ports->publish_language(ports->context, language);
    set_reply(out_reply, reply_capacity, aiqa_language_confirmation(language), NULL);
    return true;
}

bool aiqa_device_intent_controller_handle_pending_transcript(
    aiqa_device_intent_controller_t *controller,
    const char *transcript,
    uint32_t generation,
    int64_t now_us,
    const aiqa_device_intent_ports_t *ports,
    char *out_reply,
    size_t reply_capacity)
{
    if (out_reply != NULL && reply_capacity > 0U) {
        out_reply[0] = '\0';
    }
    if (controller == NULL || transcript == NULL || !controller->pending.active ||
        !ports_are_valid(ports) || out_reply == NULL || reply_capacity == 0U) {
        return false;
    }
    if (now_us > controller->pending.expires_at_us) {
        clear_pending(&controller->pending);
        return false;
    }
    if (generation != controller->pending.confirmation_generation) {
        return false;
    }
    if (is_cancellation(transcript)) {
        clear_pending(&controller->pending);
        set_reply(out_reply, reply_capacity, "已取消这次设置。", NULL);
        return true;
    }
    if (!is_confirmation(transcript)) {
        clear_pending(&controller->pending);
        return false;
    }

    aiqa_device_intent_t intent = controller->pending.intent;
    clear_pending(&controller->pending);
    if (intent.type == AIQA_DEVICE_INTENT_SET_LANGUAGE) {
        return apply_pending_language(&intent, ports, generation, out_reply, reply_capacity);
    }
    return apply_pending_profile(&intent, ports, generation, out_reply, reply_capacity);
}
