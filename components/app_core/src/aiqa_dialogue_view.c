#include "aiqa_dialogue_view.h"

#include <ctype.h>
#include <string.h>

static bool is_ascii_alnum(char value)
{
    return ((value >= '0' && value <= '9') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z'));
}

static bool is_ascii_display_char(char value)
{
    return is_ascii_alnum(value) || value == '?' || value == '!' || value == '.' || value == ',';
}

static bool ascii_contains_case_insensitive(const char *text, const char *phrase)
{
    if (text == 0 || phrase == 0 || phrase[0] == '\0') {
        return false;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        const char *text_cursor = cursor;
        const char *phrase_cursor = phrase;
        while (*text_cursor != '\0' && *phrase_cursor != '\0' &&
               toupper((unsigned char)*text_cursor) == toupper((unsigned char)*phrase_cursor)) {
            ++text_cursor;
            ++phrase_cursor;
        }
        if (*phrase_cursor == '\0') {
            return true;
        }
    }
    return false;
}

static bool text_contains(const char *text, const char *phrase)
{
    return text != 0 && phrase != 0 && strstr(text, phrase) != 0;
}

static aiqa_dialogue_emotion_t detect_pet_emotion(const char *text)
{
    if (text == 0 || text[0] == '\0') {
        return AIQA_DIALOGUE_EMOTION_NONE;
    }

    if (text_contains(text, "\xE5\xA4\xA7\xE5\x93\xAD") ||
        text_contains(text, "\xE5\x93\xAD") ||
        ascii_contains_case_insensitive(text, "CRY") ||
        ascii_contains_case_insensitive(text, "TEAR")) {
        return AIQA_DIALOGUE_EMOTION_CRYING;
    }
    if (text_contains(text, "\xE5\xA4\xA7\xE7\xAC\x91") ||
        text_contains(text, "\xE7\x88\x86\xE7\xAC\x91") ||
        ascii_contains_case_insensitive(text, "HAHA") ||
        ascii_contains_case_insensitive(text, "LOL") ||
        ascii_contains_case_insensitive(text, "FUNNY") ||
        ascii_contains_case_insensitive(text, "LAUGH")) {
        return AIQA_DIALOGUE_EMOTION_LAUGHING;
    }
    if (text_contains(text, "\xE5\xAE\xB3\xE7\xBE\x9E") ||
        text_contains(text, "\xE8\x85\xBC\xE8\x85\x86") ||
        ascii_contains_case_insensitive(text, "SHY") ||
        ascii_contains_case_insensitive(text, "BLUSH")) {
        return AIQA_DIALOGUE_EMOTION_SHY;
    }
    if (text_contains(text, "\xE6\xB2\xAE\xE4\xB8\xA7") ||
        text_contains(text, "\xE5\xA4\xB1\xE6\x9C\x9B") ||
        ascii_contains_case_insensitive(text, "FRUSTRAT") ||
        ascii_contains_case_insensitive(text, "CONFUSED")) {
        return AIQA_DIALOGUE_EMOTION_FRUSTRATED;
    }
    if (text_contains(text, "\xE9\x9A\xBE\xE8\xBF\x87") ||
        text_contains(text, "\xE4\xBC\xA4\xE5\xBF\x83") ||
        ascii_contains_case_insensitive(text, "SAD") ||
        ascii_contains_case_insensitive(text, "SORRY")) {
        return AIQA_DIALOGUE_EMOTION_SAD;
    }
    if (text_contains(text, "\xE8\xB9\xA6\xE8\xB7\xB3") ||
        ascii_contains_case_insensitive(text, "BOUNCE") ||
        ascii_contains_case_insensitive(text, "JUMP")) {
        return AIQA_DIALOGUE_EMOTION_BOUNCING;
    }
    if (text_contains(text, "\xE5\xBC\x80\xE5\xBF\x83") ||
        text_contains(text, "\xE9\xAB\x98\xE5\x85\xB4") ||
        text_contains(text, "\xE5\xBF\xAB\xE4\xB9\x90") ||
        ascii_contains_case_insensitive(text, "HAPPY") ||
        ascii_contains_case_insensitive(text, "GREAT") ||
        ascii_contains_case_insensitive(text, "NICE")) {
        return AIQA_DIALOGUE_EMOTION_HAPPY;
    }
    return AIQA_DIALOGUE_EMOTION_NONE;
}

static void write_prefixed_line(
    char *out,
    size_t out_size,
    const char *prefix,
    const char *fallback,
    const char *text)
{
    if (out == 0 || out_size == 0 || prefix == 0 || fallback == 0) {
        return;
    }

    out[0] = '\0';
    size_t pos = 0;
    for (const char *cursor = prefix; *cursor != '\0' && pos + 1 < out_size; ++cursor) {
        out[pos++] = *cursor;
    }

    bool wrote_content = false;
    bool previous_space = pos > 0 && out[pos - 1] == ' ';
    for (const char *cursor = text; cursor != 0 && *cursor != '\0' && pos + 1 < out_size; ++cursor) {
        unsigned char raw_value = (unsigned char)*cursor;
        if (raw_value > 0x7f) {
            continue;
        }

        char value = (char)raw_value;
        if (value == '\n' || value == '\r' || value == '\t') {
            value = ' ';
        }
        if (value == ' ') {
            if (!previous_space && wrote_content && pos + 1 < out_size) {
                out[pos++] = ' ';
                previous_space = true;
            }
            continue;
        }
        if (!is_ascii_display_char(value)) {
            continue;
        }
        out[pos++] = (char)toupper((unsigned char)value);
        wrote_content = wrote_content || is_ascii_alnum(value);
        previous_space = false;
    }

    if (!wrote_content) {
        pos = 0;
        for (const char *cursor = fallback; *cursor != '\0' && pos + 1 < out_size; ++cursor) {
            out[pos++] = *cursor;
        }
    }
    if (pos > 0 && out[pos - 1] == ' ') {
        --pos;
    }
    out[pos] = '\0';
}

void aiqa_dialogue_view_clear(aiqa_dialogue_view_t *view)
{
    if (view == 0) {
        return;
    }
    *view = (aiqa_dialogue_view_t){0};
}

void aiqa_dialogue_view_set_user(aiqa_dialogue_view_t *view, const char *text)
{
    if (view == 0) {
        return;
    }
    write_prefixed_line(view->user_line, sizeof(view->user_line), "YOU ", "YOU VOICE RECEIVED", text);
    view->has_dialogue = true;
}

void aiqa_dialogue_view_set_pet(aiqa_dialogue_view_t *view, const char *text)
{
    if (view == 0) {
        return;
    }
    view->pet_emotion = detect_pet_emotion(text);
    write_prefixed_line(view->pet_line, sizeof(view->pet_line), "PET ", "PET ANSWER READY", text);
    view->has_dialogue = true;
}
