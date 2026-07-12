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
    write_prefixed_line(view->pet_line, sizeof(view->pet_line), "PET ", "PET ANSWER READY", text);
    view->has_dialogue = true;
}
