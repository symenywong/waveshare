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

static bool copy_name_after_marker(
    const char *text,
    const char *marker,
    char *out_text,
    size_t out_text_size)
{
    const char *start = strstr(text, marker);
    if (start == NULL || out_text == NULL || out_text_size == 0) {
        return false;
    }
    start += strlen(marker);
    skip_ascii_spaces(&start);
    if (*start == '\0') {
        return false;
    }

    size_t pos = 0;
    while (start[pos] != '\0' && pos + 1 < out_text_size) {
        if (((unsigned char)start[pos]) < 0x80 &&
            (start[pos] == ' ' || start[pos] == ',' || start[pos] == '.')) {
            break;
        }
        out_text[pos] = start[pos];
        ++pos;
    }
    out_text[pos] = '\0';
    return pos > 0 && start[pos] == '\0';
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

    if (copy_name_after_marker(transcript, "把名字改成", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "名字改成", out_command->text, sizeof(out_command->text)) ||
        copy_name_after_marker(transcript, "你叫", out_command->text, sizeof(out_command->text))) {
        out_command->type = AIQA_LOCAL_COMMAND_SET_NAME;
        return true;
    }

    if (contains(transcript, "女声") || contains(transcript, "女性")) {
        out_command->type = AIQA_LOCAL_COMMAND_SET_GENDER;
        out_command->gender = AIQA_ASSISTANT_GENDER_FEMALE;
        return true;
    }
    if (contains(transcript, "男声") || contains(transcript, "男性")) {
        out_command->type = AIQA_LOCAL_COMMAND_SET_GENDER;
        out_command->gender = AIQA_ASSISTANT_GENDER_MALE;
        return true;
    }
    if (contains(transcript, "中性")) {
        out_command->type = AIQA_LOCAL_COMMAND_SET_GENDER;
        out_command->gender = AIQA_ASSISTANT_GENDER_NEUTRAL;
        return true;
    }

    return false;
}
