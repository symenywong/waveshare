#include "aiqa_language.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static char ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static bool contains_ascii_phrase(const char *text, const char *phrase)
{
    if (text == NULL || phrase == NULL || phrase[0] == '\0') {
        return false;
    }

    const size_t phrase_len = strlen(phrase);
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        size_t index = 0;
        while (index < phrase_len &&
               cursor[index] != '\0' &&
               ascii_lower(cursor[index]) == ascii_lower(phrase[index])) {
            ++index;
        }
        if (index == phrase_len) {
            return true;
        }
    }
    return false;
}

static bool contains_any_utf8_phrase(const char *text, const char *const *phrases, size_t phrase_count)
{
    if (text == NULL) {
        return false;
    }
    for (size_t index = 0; index < phrase_count; ++index) {
        if (strstr(text, phrases[index]) != NULL) {
            return true;
        }
    }
    return false;
}

static bool contains_any_ascii_phrase(const char *text, const char *const *phrases, size_t phrase_count)
{
    for (size_t index = 0; index < phrase_count; ++index) {
        if (contains_ascii_phrase(text, phrases[index])) {
            return true;
        }
    }
    return false;
}

static bool has_translation_question_context(const char *text)
{
    static const char *const utf8_phrases[] = {
        "怎么说",
        "怎麼說",
        "怎么翻译",
        "怎麼翻譯",
        "翻译成",
        "翻譯成",
        "如何表达",
        "如何表達",
    };
    static const char *const ascii_phrases[] = {
        "how do you say",
        "how to say",
        "translate",
    };

    return contains_any_utf8_phrase(text, utf8_phrases, sizeof(utf8_phrases) / sizeof(utf8_phrases[0])) ||
           contains_any_ascii_phrase(text, ascii_phrases, sizeof(ascii_phrases) / sizeof(ascii_phrases[0]));
}

aiqa_dialogue_language_t aiqa_language_default(void)
{
    return AIQA_DIALOGUE_LANGUAGE_CHINESE;
}

bool aiqa_language_is_valid(aiqa_dialogue_language_t language)
{
    return language == AIQA_DIALOGUE_LANGUAGE_ENGLISH ||
           language == AIQA_DIALOGUE_LANGUAGE_CHINESE;
}

bool aiqa_language_detect_switch_command(const char *text, aiqa_dialogue_language_t *out_language)
{
    static const char *const chinese_phrases[] = {
        "使用中文",
        "用中文",
        "说中文",
        "講中文",
        "讲中文",
        "切换到中文",
        "切換到中文",
        "中文模式",
        "中文与我交流",
        "中文和我交流",
        "請用中文",
        "请用中文",
        "用普通话",
        "说普通话",
    };
    static const char *const chinese_ascii_phrases[] = {
        "speak chinese",
        "use chinese",
        "in chinese",
        "chinese mode",
    };
    static const char *const english_phrases[] = {
        "使用英文",
        "使用英语",
        "使用英語",
        "用英文",
        "用英语",
        "用英語",
        "说英文",
        "说英语",
        "說英語",
        "讲英文",
        "讲英语",
        "講英語",
        "切换到英文",
        "切换到英语",
        "切換到英語",
        "英文模式",
        "英语模式",
        "英語模式",
        "英文与我交流",
        "英语与我交流",
        "英語與我交流",
    };
    static const char *const english_ascii_phrases[] = {
        "speak english",
        "use english",
        "in english",
        "english mode",
    };

    if (text == NULL || text[0] == '\0' || out_language == NULL) {
        return false;
    }
    if (has_translation_question_context(text)) {
        return false;
    }
    if (contains_any_utf8_phrase(text, chinese_phrases, sizeof(chinese_phrases) / sizeof(chinese_phrases[0])) ||
        contains_any_ascii_phrase(text, chinese_ascii_phrases,
                                  sizeof(chinese_ascii_phrases) / sizeof(chinese_ascii_phrases[0]))) {
        *out_language = AIQA_DIALOGUE_LANGUAGE_CHINESE;
        return true;
    }
    if (contains_any_utf8_phrase(text, english_phrases, sizeof(english_phrases) / sizeof(english_phrases[0])) ||
        contains_any_ascii_phrase(text, english_ascii_phrases,
                                  sizeof(english_ascii_phrases) / sizeof(english_ascii_phrases[0]))) {
        *out_language = AIQA_DIALOGUE_LANGUAGE_ENGLISH;
        return true;
    }
    return false;
}

const char *aiqa_language_confirmation(aiqa_dialogue_language_t language)
{
    switch (language) {
    case AIQA_DIALOGUE_LANGUAGE_CHINESE:
        return "好的，我会用中文和你交流。";
    case AIQA_DIALOGUE_LANGUAGE_ENGLISH:
    default:
        return "Sure, I will speak English with you.";
    }
}

const char *aiqa_language_display_label(aiqa_dialogue_language_t language)
{
    switch (language) {
    case AIQA_DIALOGUE_LANGUAGE_CHINESE:
        return "CHINESE MODE";
    case AIQA_DIALOGUE_LANGUAGE_ENGLISH:
    default:
        return "ENGLISH MODE";
    }
}

const char *aiqa_language_chat_code(aiqa_dialogue_language_t language)
{
    switch (language) {
    case AIQA_DIALOGUE_LANGUAGE_CHINESE:
        return "zh";
    case AIQA_DIALOGUE_LANGUAGE_ENGLISH:
    default:
        return "en";
    }
}

bool aiqa_language_from_chat_code(
    const char *language_code,
    aiqa_dialogue_language_t *out_language)
{
    if (language_code == NULL || out_language == NULL) {
        return false;
    }
    if (strcmp(language_code, "zh") == 0) {
        *out_language = AIQA_DIALOGUE_LANGUAGE_CHINESE;
        return true;
    }
    if (strcmp(language_code, "en") == 0) {
        *out_language = AIQA_DIALOGUE_LANGUAGE_ENGLISH;
        return true;
    }
    return false;
}

const char *aiqa_language_name(aiqa_dialogue_language_t language)
{
    switch (language) {
    case AIQA_DIALOGUE_LANGUAGE_CHINESE:
        return "Chinese";
    case AIQA_DIALOGUE_LANGUAGE_ENGLISH:
    default:
        return "English";
    }
}
