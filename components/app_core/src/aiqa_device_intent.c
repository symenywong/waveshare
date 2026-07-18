#include "aiqa_device_intent.h"

#include <stdint.h>
#include <string.h>

static size_t bounded_strlen(const char *value, size_t capacity)
{
    size_t length = 0U;
    if (value == NULL) {
        return 0U;
    }
    while (length < capacity && value[length] != '\0') {
        length += 1U;
    }
    return length;
}

static bool text_is_bounded_without_controls(const char *value, size_t capacity)
{
    const size_t length = bounded_strlen(value, capacity);
    if (length == 0U || length >= capacity) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t byte = (uint8_t)value[index];
        if (byte < 0x20U || byte == 0x7fU) {
            return false;
        }
    }
    return true;
}

static bool contains_any(
    const char *text,
    const char *const *markers,
    size_t marker_count)
{
    for (size_t index = 0U; index < marker_count; ++index) {
        if (strstr(text, markers[index]) != NULL) {
            return true;
        }
    }
    return false;
}

static uint8_t ascii_lower(uint8_t byte)
{
    return byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte + ('a' - 'A')) : byte;
}

static bool is_ascii_letter(uint8_t byte)
{
    const uint8_t lower = ascii_lower(byte);
    return lower >= 'a' && lower <= 'z';
}

static bool contains_ascii_word(const char *text, const char *word)
{
    const size_t text_length = strlen(text);
    const size_t word_length = strlen(word);
    for (size_t start = 0U; start + word_length <= text_length; ++start) {
        if ((start > 0U && is_ascii_letter((uint8_t)text[start - 1U])) ||
            (start + word_length < text_length &&
             is_ascii_letter((uint8_t)text[start + word_length]))) {
            continue;
        }
        size_t offset = 0U;
        while (offset < word_length &&
               ascii_lower((uint8_t)text[start + offset]) ==
                   ascii_lower((uint8_t)word[offset])) {
            offset += 1U;
        }
        if (offset == word_length) {
            return true;
        }
    }
    return false;
}

static bool gender_evidence_matches(const char *value, const char *evidence)
{
    static const char *const FEMALE[] = {"女"};
    static const char *const MALE[] = {"男"};
    static const char *const NEUTRAL[] = {"中性", "无性别", "不分性别"};
    const bool female = contains_any(evidence, FEMALE, sizeof(FEMALE) / sizeof(FEMALE[0])) ||
                        contains_ascii_word(evidence, "female") ||
                        contains_ascii_word(evidence, "woman") ||
                        contains_ascii_word(evidence, "girl");
    const bool male = contains_any(evidence, MALE, sizeof(MALE) / sizeof(MALE[0])) ||
                      contains_ascii_word(evidence, "male") ||
                      contains_ascii_word(evidence, "man") ||
                      contains_ascii_word(evidence, "boy");
    const bool neutral =
        contains_any(evidence, NEUTRAL, sizeof(NEUTRAL) / sizeof(NEUTRAL[0])) ||
        contains_ascii_word(evidence, "neutral");
    if (strcmp(value, "female") == 0) {
        return female && !male && !neutral;
    }
    if (strcmp(value, "male") == 0) {
        return male && !female && !neutral;
    }
    return strcmp(value, "neutral") == 0 && neutral && !female && !male;
}

static bool language_evidence_matches(const char *value, const char *evidence)
{
    static const char *const CHINESE[] = {"中文", "汉语", "普通话"};
    static const char *const ENGLISH[] = {"英语", "英文"};
    const bool chinese = contains_any(evidence, CHINESE, sizeof(CHINESE) / sizeof(CHINESE[0])) ||
                         contains_ascii_word(evidence, "chinese");
    const bool english = contains_any(evidence, ENGLISH, sizeof(ENGLISH) / sizeof(ENGLISH[0])) ||
                         contains_ascii_word(evidence, "english");
    if (strcmp(value, "zh") == 0) {
        return chinese && !english;
    }
    return strcmp(value, "en") == 0 && english && !chinese;
}

static bool query_evidence_matches(
    aiqa_device_intent_type_t type,
    const char *evidence)
{
    static const char *const NAME[] = {"名字", "叫什么", "称呼", "name", "Name"};
    static const char *const GENDER[] = {"性别", "男", "女", "gender", "Gender"};
    static const char *const LANGUAGE[] = {
        "语言", "中文", "汉语", "普通话", "英语", "英文", "language", "Language",
    };
    if (type == AIQA_DEVICE_INTENT_GET_PROFILE_NAME) {
        return contains_any(evidence, NAME, sizeof(NAME) / sizeof(NAME[0]));
    }
    if (type == AIQA_DEVICE_INTENT_GET_PROFILE_GENDER) {
        return contains_any(evidence, GENDER, sizeof(GENDER) / sizeof(GENDER[0]));
    }
    return type == AIQA_DEVICE_INTENT_GET_PROFILE_LANGUAGE &&
           contains_any(evidence, LANGUAGE, sizeof(LANGUAGE) / sizeof(LANGUAGE[0]));
}

bool aiqa_device_intent_is_setting(aiqa_device_intent_type_t type)
{
    return type == AIQA_DEVICE_INTENT_SET_NAME ||
           type == AIQA_DEVICE_INTENT_SET_GENDER ||
           type == AIQA_DEVICE_INTENT_SET_LANGUAGE;
}

bool aiqa_device_intent_is_valid(
    const aiqa_device_intent_t *intent,
    const char *transcript)
{
    if (intent == NULL || transcript == NULL || transcript[0] == '\0') {
        return false;
    }
    if (intent->type == AIQA_DEVICE_INTENT_NONE) {
        return intent->value[0] == '\0' && intent->evidence[0] == '\0';
    }
    if (!text_is_bounded_without_controls(
            intent->evidence, sizeof(intent->evidence)) ||
        strstr(transcript, intent->evidence) == NULL) {
        return false;
    }
    switch (intent->type) {
    case AIQA_DEVICE_INTENT_SET_NAME: {
        aiqa_assistant_profile_t profile = aiqa_assistant_profile_default();
        return strstr(intent->evidence, intent->value) != NULL &&
               aiqa_assistant_profile_set_name(&profile, intent->value);
    }
    case AIQA_DEVICE_INTENT_SET_GENDER:
        return gender_evidence_matches(intent->value, intent->evidence);
    case AIQA_DEVICE_INTENT_SET_LANGUAGE:
        return language_evidence_matches(intent->value, intent->evidence);
    case AIQA_DEVICE_INTENT_GET_PROFILE_NAME:
    case AIQA_DEVICE_INTENT_GET_PROFILE_GENDER:
    case AIQA_DEVICE_INTENT_GET_PROFILE_LANGUAGE:
        return intent->value[0] == '\0' &&
               query_evidence_matches(intent->type, intent->evidence);
    default:
        return false;
    }
}

void aiqa_device_intent_clear(aiqa_device_intent_t *intent)
{
    if (intent == NULL) {
        return;
    }
    volatile uint8_t *bytes = (volatile uint8_t *)intent;
    size_t remaining = sizeof(*intent);
    while (remaining > 0U) {
        *bytes++ = 0U;
        remaining -= 1U;
    }
}
