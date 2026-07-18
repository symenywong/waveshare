#include "aiqa_local_command.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool contains(const char *text, const char *needle)
{
    return text != NULL && needle != NULL && strstr(text, needle) != NULL;
}

uint8_t aiqa_local_command_clamp_volume(int volume_percent)
{
    if (volume_percent < 0) {
        return 0;
    }
    if (volume_percent > 100) {
        return 100;
    }
    return (uint8_t)volume_percent;
}

static bool parse_first_int(const char *text, int *out_value)
{
    if (text == NULL || out_value == NULL) {
        return false;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor >= '0' && *cursor <= '9') {
            *out_value = atoi(cursor);
            return true;
        }
    }
    return false;
}

static void skip_ascii_spaces(const char **cursor)
{
    while (cursor != NULL && *cursor != NULL &&
           (**cursor == ' ' || **cursor == '\t' || **cursor == ':')) {
        ++(*cursor);
    }
}

static bool has_suffix(const char *text, size_t text_len, const char *suffix)
{
    const size_t suffix_len = strlen(suffix);
    return suffix_len <= text_len &&
           memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

static bool has_question_context(const char *text, const char *name, size_t name_len)
{
    return contains(text, "?") ||
           contains(text, "？") ||
           contains(text, "叫什么") ||
           contains(text, "名字是什么") ||
           contains(text, "名字叫啥") ||
           contains(text, "哪个") ||
           contains(text, "还是") ||
           (name_len >= strlen("什么") && memcmp(name, "什么", strlen("什么")) == 0) ||
           (name_len >= strlen("啥") && memcmp(name, "啥", strlen("啥")) == 0) ||
           (name_len >= strlen("谁") && memcmp(name, "谁", strlen("谁")) == 0) ||
           has_suffix(name, name_len, "吗") ||
           has_suffix(name, name_len, "嘛") ||
           has_suffix(name, name_len, "呢") ||
           has_suffix(name, name_len, "吧") ||
           has_suffix(name, name_len, "对吧") ||
           has_suffix(name, name_len, "谢谢");
}

static void trim_name_end(const char *start, size_t *length)
{
    while (*length > 0) {
        const unsigned char last = (unsigned char)start[*length - 1];
        if (last == ' ' || last == '\t' || last == ',' || last == '.' || last == '!') {
            --(*length);
            continue;
        }
        if (has_suffix(start, *length, "。") || has_suffix(start, *length, "！") ||
            has_suffix(start, *length, "”") || has_suffix(start, *length, "」")) {
            *length -= 3;
            continue;
        }
        break;
    }
}

static bool unquoted_name_shape_is_clear(const char *name, size_t name_len)
{
    bool contains_non_ascii = false;
    size_t codepoint_count = 0;
    for (size_t index = 0; index < name_len; ++index) {
        const unsigned char byte = (unsigned char)name[index];
        if ((byte & 0xc0U) != 0x80U) {
            codepoint_count += 1U;
        }
        if (byte >= 0x80U) {
            contains_non_ascii = true;
        }
    }
    /* Longer CJK names remain supported when quotes make the boundary explicit. */
    return !contains_non_ascii || codepoint_count <= 4U;
}

static bool copy_name_after_marker(
    const char *text,
    const char *marker,
    char *out_text,
    size_t out_text_size)
{
    if (text == NULL || marker == NULL || out_text == NULL || out_text_size == 0) {
        return false;
    }
    const char *start = text;
    if (strncmp(start, "请", 3) == 0) {
        start += 3;
        skip_ascii_spaces(&start);
    }
    const size_t marker_len = strlen(marker);
    if (strncmp(start, marker, marker_len) != 0) {
        return false;
    }
    start += marker_len;
    skip_ascii_spaces(&start);
    if (*start == '\0') {
        return false;
    }

    const bool double_quoted = strncmp(start, "“", 3) == 0;
    const bool corner_quoted = strncmp(start, "「", 3) == 0;
    const bool quoted = double_quoted || corner_quoted;
    if (quoted) {
        start += 3;
    }

    size_t name_len = 0;
    if (quoted) {
        const char *closing_quote = strstr(start, double_quoted ? "”" : "」");
        if (closing_quote == NULL) {
            return false;
        }
        name_len = (size_t)(closing_quote - start);
        const char *trailing = closing_quote + 3;
        size_t trailing_len = strlen(trailing);
        trim_name_end(trailing, &trailing_len);
        if (trailing_len != 0U) {
            return false;
        }
    } else {
        name_len = strlen(start);
        trim_name_end(start, &name_len);
    }
    if (has_question_context(text, start, name_len)) {
        return false;
    }
    if (name_len == 0 || name_len >= out_text_size ||
        (!quoted && !unquoted_name_shape_is_clear(start, name_len))) {
        return false;
    }
    (void)memcpy(out_text, start, name_len);
    out_text[name_len] = '\0';
    return true;
}

static bool command_equals(const char *transcript, const char *expected)
{
    const char *start = transcript;
    if (strncmp(start, "请", 3) == 0) {
        start += 3;
        skip_ascii_spaces(&start);
    }
    size_t length = strlen(start);
    trim_name_end(start, &length);
    return length == strlen(expected) && memcmp(start, expected, length) == 0;
}

static bool is_female_update(const char *transcript)
{
    return command_equals(transcript, "女声") ||
           command_equals(transcript, "你是女声") ||
           command_equals(transcript, "你是女性") ||
           command_equals(transcript, "性别设为女性") ||
           command_equals(transcript, "性别设置为女性") ||
           command_equals(transcript, "把性别改成女性") ||
           command_equals(transcript, "设为女声") ||
           command_equals(transcript, "设置为女声") ||
           command_equals(transcript, "改成女声") ||
           command_equals(transcript, "使用女声") ||
           command_equals(transcript, "切换到女声");
}

static bool is_male_update(const char *transcript)
{
    return command_equals(transcript, "男声") ||
           command_equals(transcript, "你是男声") ||
           command_equals(transcript, "你是男性") ||
           command_equals(transcript, "性别设为男性") ||
           command_equals(transcript, "性别设置为男性") ||
           command_equals(transcript, "把性别改成男性") ||
           command_equals(transcript, "设为男声") ||
           command_equals(transcript, "设置为男声") ||
           command_equals(transcript, "改成男声") ||
           command_equals(transcript, "使用男声") ||
           command_equals(transcript, "切换到男声");
}

static bool is_neutral_update(const char *transcript)
{
    return command_equals(transcript, "中性") ||
           command_equals(transcript, "你是中性") ||
           command_equals(transcript, "性别设为中性") ||
           command_equals(transcript, "性别设置为中性") ||
           command_equals(transcript, "把性别改成中性") ||
           command_equals(transcript, "设为中性") ||
           command_equals(transcript, "设置为中性") ||
           command_equals(transcript, "改成中性") ||
           command_equals(transcript, "使用中性声音") ||
           command_equals(transcript, "切换到中性声音");
}

static bool set_gender_command(
    aiqa_local_command_t *out_command,
    aiqa_assistant_gender_t gender)
{
    if (out_command == NULL) {
        return false;
    }
    out_command->type = AIQA_LOCAL_COMMAND_SET_GENDER;
    out_command->gender = gender;
    return true;
}

bool aiqa_local_command_parse(const char *transcript, aiqa_local_command_t *out_command)
{
    if (transcript == NULL || out_command == NULL || transcript[0] == '\0') {
        return false;
    }
    *out_command = (aiqa_local_command_t){0};

    if (contains(transcript, "音量") || contains(transcript, "静音")) {
        if (contains(transcript, "当前") || contains(transcript, "多少")) {
            out_command->type = AIQA_LOCAL_COMMAND_VOLUME_QUERY;
            return true;
        }
        if (contains(transcript, "静音")) {
            out_command->type = AIQA_LOCAL_COMMAND_VOLUME_SET;
            out_command->value = 0;
            return true;
        }
        if (contains(transcript, "调大") || contains(transcript, "大一点") ||
            contains(transcript, "增大") || contains(transcript, "提高")) {
            out_command->type = AIQA_LOCAL_COMMAND_VOLUME_RELATIVE;
            out_command->value = 10;
            return true;
        }
        if (contains(transcript, "调小") || contains(transcript, "小一点") ||
            contains(transcript, "降低") || contains(transcript, "减小")) {
            out_command->type = AIQA_LOCAL_COMMAND_VOLUME_RELATIVE;
            out_command->value = -10;
            return true;
        }
        int value = 0;
        if (parse_first_int(transcript, &value)) {
            out_command->type = AIQA_LOCAL_COMMAND_VOLUME_SET;
            out_command->value = aiqa_local_command_clamp_volume(value);
            return true;
        }
    }

    if (contains(transcript, "电量") || contains(transcript, "多少电") ||
        contains(transcript, "充电")) {
        out_command->type = AIQA_LOCAL_COMMAND_BATTERY_QUERY;
        return true;
    }

    if (contains(transcript, "星期")) {
        out_command->type = AIQA_LOCAL_COMMAND_WEEKDAY_QUERY;
        return true;
    }
    if (contains(transcript, "几点") || contains(transcript, "现在时间")) {
        out_command->type = AIQA_LOCAL_COMMAND_TIME_QUERY;
        return true;
    }
    if (contains(transcript, "日期") || contains(transcript, "今天")) {
        out_command->type = AIQA_LOCAL_COMMAND_DATE_QUERY;
        return true;
    }

    if (copy_name_after_marker(transcript, "形象的名字叫", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "形象名字叫", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "你的名字叫", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "你的名字是", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "以后你就叫", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "以后就叫", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "从现在开始你叫", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "我给你取名", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "给你取名", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "把名字改成", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "名字改成", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "名字叫", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "你叫", out_command->text, sizeof(out_command->text))) {
        out_command->type = AIQA_LOCAL_COMMAND_SET_NAME;
        return true;
    }

    if (is_female_update(transcript)) {
        return set_gender_command(out_command, AIQA_ASSISTANT_GENDER_FEMALE);
    }
    if (is_male_update(transcript)) {
        return set_gender_command(out_command, AIQA_ASSISTANT_GENDER_MALE);
    }
    if (is_neutral_update(transcript)) {
        return set_gender_command(out_command, AIQA_ASSISTANT_GENDER_NEUTRAL);
    }

    return false;
}
