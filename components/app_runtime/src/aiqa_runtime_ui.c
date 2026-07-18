#include "aiqa_runtime_ui.h"

#include <string.h>

static bool text_contains(const char *text, const char *phrase)
{
    return text != NULL && phrase != NULL && strstr(text, phrase) != NULL;
}

static board_wave_175c_pet_expression_t ui_expression_for_dialogue_emotion(
    aiqa_dialogue_emotion_t emotion)
{
    switch (emotion) {
    case AIQA_DIALOGUE_EMOTION_HAPPY:
        return BOARD_WAVE_175C_PET_EXPRESSION_HAPPY;
    case AIQA_DIALOGUE_EMOTION_SAD:
        return BOARD_WAVE_175C_PET_EXPRESSION_SAD;
    case AIQA_DIALOGUE_EMOTION_SHY:
        return BOARD_WAVE_175C_PET_EXPRESSION_SHY;
    case AIQA_DIALOGUE_EMOTION_FRUSTRATED:
        return BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED;
    case AIQA_DIALOGUE_EMOTION_BOUNCING:
        return BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING;
    case AIQA_DIALOGUE_EMOTION_LAUGHING:
        return BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING;
    case AIQA_DIALOGUE_EMOTION_CRYING:
        return BOARD_WAVE_175C_PET_EXPRESSION_CRYING;
    case AIQA_DIALOGUE_EMOTION_NONE:
    default:
        return BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING;
    }
}

static bool ui_has_active_error(aiqa_error_code_t error)
{
    return error != AIQA_ERROR_NONE && error != AIQA_ERROR_CANCELLED;
}

static bool ui_marks_error(aiqa_error_code_t error)
{
    return ui_has_active_error(error) && error != AIQA_ERROR_CONFIG_MISSING;
}

static const char *ui_status_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (ui_has_active_error(error)) {
        switch (error) {
        case AIQA_ERROR_CONFIG_MISSING:
            return "SETUP";
        case AIQA_ERROR_CONFIG_CORRUPT:
            return "CONFIG";
        case AIQA_ERROR_NETWORK_FAILED:
            return "WIFI";
        case AIQA_ERROR_AUTH_FAILED:
            return "AUTH";
        case AIQA_ERROR_TLS_FAILED:
            return "TLS";
        case AIQA_ERROR_CERT_TIME_INVALID:
            return "TIME";
        case AIQA_ERROR_RATE_LIMITED:
            return "WAIT";
        case AIQA_ERROR_PROVIDER_UNSUPPORTED:
            return "MODEL";
        case AIQA_ERROR_AUDIO_TOO_LONG:
            return "TOO LONG";
        case AIQA_ERROR_ASR_FAILED:
            return "ASR";
        case AIQA_ERROR_CHAT_FAILED:
            return "CHAT";
        case AIQA_ERROR_TIMEOUT:
            return "TIMEOUT";
        default:
            return "ERROR";
        }
    }

    switch (state) {
    case AIQA_STATE_BOOT:
        return "BOOT";
    case AIQA_STATE_CONFIG_CHECK:
        return "CONFIG";
    case AIQA_STATE_NETWORK_CONNECTING:
        return "WIFI";
    case AIQA_STATE_IDLE:
        return "READY";
    case AIQA_STATE_IDLE_WITH_RESULT:
        return "SPEAK";
    case AIQA_STATE_RECORDING:
        return "LISTEN";
    case AIQA_STATE_TRANSCRIBING:
    case AIQA_STATE_ASR_JOB_PENDING:
        return "ASR";
    case AIQA_STATE_THINKING:
        return "THINK";
    case AIQA_STATE_ERROR:
        return "READY";
    default:
        return "UNKNOWN";
    }
}

static const char *ui_detail_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (ui_has_active_error(error)) {
        switch (error) {
        case AIQA_ERROR_CONFIG_MISSING:
            return "PAIR";
        case AIQA_ERROR_CONFIG_CORRUPT:
            return "PAIR";
        case AIQA_ERROR_NETWORK_FAILED:
            return "WIFI";
        case AIQA_ERROR_AUTH_FAILED:
            return "KEY";
        case AIQA_ERROR_TLS_FAILED:
        case AIQA_ERROR_CERT_TIME_INVALID:
            return "CLOCK";
        case AIQA_ERROR_RATE_LIMITED:
            return "WAIT";
        case AIQA_ERROR_PROVIDER_UNSUPPORTED:
            return "MODEL";
        case AIQA_ERROR_AUDIO_TOO_LONG:
            return "SHORTER";
        case AIQA_ERROR_ASR_FAILED:
            return "VOICE";
        case AIQA_ERROR_CHAT_FAILED:
            return "MODEL";
        case AIQA_ERROR_TIMEOUT:
            return "RETRY";
        default:
            return NULL;
        }
    }

    switch (state) {
    case AIQA_STATE_BOOT:
        return NULL;
    case AIQA_STATE_CONFIG_CHECK:
        return NULL;
    case AIQA_STATE_NETWORK_CONNECTING:
        return "CONNECT";
    case AIQA_STATE_IDLE:
        return NULL;
    case AIQA_STATE_RECORDING:
        return "IN VOICE";
    case AIQA_STATE_TRANSCRIBING:
    case AIQA_STATE_ASR_JOB_PENDING:
        return "TO TEXT";
    case AIQA_STATE_THINKING:
        return "OUT WAIT";
    case AIQA_STATE_IDLE_WITH_RESULT:
        return "OUT READY";
    default:
        return NULL;
    }
}

static const char *ui_hint_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (error == AIQA_ERROR_CONFIG_MISSING) {
        return "PAIR";
    }
    if (error == AIQA_ERROR_AUDIO_TOO_LONG ||
        error == AIQA_ERROR_ASR_FAILED ||
        error == AIQA_ERROR_CHAT_FAILED ||
        error == AIQA_ERROR_TIMEOUT) {
        return "RETRY";
    }
    if (error == AIQA_ERROR_NETWORK_FAILED) {
        return "WIFI";
    }
    if (error == AIQA_ERROR_AUTH_FAILED ||
        error == AIQA_ERROR_PROVIDER_UNSUPPORTED) {
        return "KEY";
    }
    if (error == AIQA_ERROR_TLS_FAILED ||
        error == AIQA_ERROR_CERT_TIME_INVALID) {
        return "CLOCK";
    }
    if (error == AIQA_ERROR_RATE_LIMITED) {
        return "WAIT";
    }
    if (ui_has_active_error(error)) {
        return NULL;
    }

    switch (state) {
    case AIQA_STATE_IDLE:
    case AIQA_STATE_IDLE_WITH_RESULT:
        return "HOLD BOOT";
    case AIQA_STATE_RECORDING:
        return "RELEASE";
    case AIQA_STATE_NETWORK_CONNECTING:
        return "WAIT";
    default:
        return NULL;
    }
}

static uint16_t ui_accent_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (ui_has_active_error(error)) {
        return 0xFD20;
    }

    switch (state) {
    case AIQA_STATE_BOOT:
        return 0x001F;
    case AIQA_STATE_CONFIG_CHECK:
        return 0xFFE0;
    case AIQA_STATE_NETWORK_CONNECTING:
        return 0x07FF;
    case AIQA_STATE_IDLE:
    case AIQA_STATE_IDLE_WITH_RESULT:
        return 0x07E0;
    case AIQA_STATE_RECORDING:
        return 0xF81F;
    case AIQA_STATE_TRANSCRIBING:
    case AIQA_STATE_ASR_JOB_PENDING:
    case AIQA_STATE_THINKING:
        return 0xFD20;
    default:
        return 0xFFFF;
    }
}

static board_wave_175c_pet_expression_t ui_expression_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (ui_has_active_error(error)) {
        switch (error) {
        case AIQA_ERROR_CONFIG_MISSING:
            return BOARD_WAVE_175C_PET_EXPRESSION_SHY;
        case AIQA_ERROR_ASR_FAILED:
            return BOARD_WAVE_175C_PET_EXPRESSION_SAD;
        case AIQA_ERROR_CHAT_FAILED:
            return BOARD_WAVE_175C_PET_EXPRESSION_CRYING;
        case AIQA_ERROR_TIMEOUT:
        case AIQA_ERROR_RATE_LIMITED:
        case AIQA_ERROR_AUDIO_TOO_LONG:
            return BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED;
        default:
            return BOARD_WAVE_175C_PET_EXPRESSION_WORRIED;
        }
    }

    switch (state) {
    case AIQA_STATE_BOOT:
        return BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY;
    case AIQA_STATE_CONFIG_CHECK:
        return BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS;
    case AIQA_STATE_NETWORK_CONNECTING:
        return BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING;
    case AIQA_STATE_RECORDING:
        return BOARD_WAVE_175C_PET_EXPRESSION_LISTENING;
    case AIQA_STATE_TRANSCRIBING:
    case AIQA_STATE_ASR_JOB_PENDING:
        return BOARD_WAVE_175C_PET_EXPRESSION_LISTENING;
    case AIQA_STATE_THINKING:
        return BOARD_WAVE_175C_PET_EXPRESSION_THINKING;
    case AIQA_STATE_IDLE_WITH_RESULT:
        return BOARD_WAVE_175C_PET_EXPRESSION_HAPPY;
    case AIQA_STATE_IDLE:
        return BOARD_WAVE_175C_PET_EXPRESSION_IDLE;
    default:
        return BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS;
    }
}

static board_wave_175c_pet_expression_t ui_dialogue_expression_for(
    const aiqa_dialogue_view_t *dialogue)
{
    if (dialogue == NULL) {
        return BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING;
    }
    if (dialogue->pet_emotion != AIQA_DIALOGUE_EMOTION_NONE) {
        return ui_expression_for_dialogue_emotion(dialogue->pet_emotion);
    }

    const char *pet_line = dialogue->pet_line;
    if (text_contains(pet_line, "HAHA") ||
        text_contains(pet_line, "LOL") ||
        text_contains(pet_line, "FUNNY") ||
        text_contains(pet_line, "LAUGH")) {
        return BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING;
    }
    if (text_contains(pet_line, "SHY") ||
        text_contains(pet_line, "BLUSH")) {
        return BOARD_WAVE_175C_PET_EXPRESSION_SHY;
    }
    if (text_contains(pet_line, "CRY") ||
        text_contains(pet_line, "TEAR")) {
        return BOARD_WAVE_175C_PET_EXPRESSION_CRYING;
    }
    if (text_contains(pet_line, "SAD") ||
        text_contains(pet_line, "SORRY")) {
        return BOARD_WAVE_175C_PET_EXPRESSION_SAD;
    }
    if (text_contains(pet_line, "FRUSTRAT") ||
        text_contains(pet_line, "CONFUSED")) {
        return BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED;
    }
    if (text_contains(pet_line, "HAPPY") ||
        text_contains(pet_line, "GREAT") ||
        text_contains(pet_line, "NICE")) {
        return BOARD_WAVE_175C_PET_EXPRESSION_HAPPY;
    }
    return BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING;
}

board_wave_175c_display_page_t aiqa_runtime_ui_page_for(aiqa_state_t state, aiqa_error_code_t error)
{
    return aiqa_runtime_ui_page_for_dialogue(state, error, NULL);
}

board_wave_175c_display_page_t aiqa_runtime_ui_page_for_dialogue(
    aiqa_state_t state,
    aiqa_error_code_t error,
    const aiqa_dialogue_view_t *dialogue)
{
    const bool can_show_dialogue = error == AIQA_ERROR_NONE &&
                                   dialogue != NULL &&
                                   dialogue->has_dialogue;
    const bool has_user_line = can_show_dialogue && dialogue->user_line[0] != '\0';
    const bool has_pet_line = can_show_dialogue && dialogue->pet_line[0] != '\0';
    const bool show_answer_expression = has_pet_line && state == AIQA_STATE_IDLE_WITH_RESULT;
    board_wave_175c_pet_expression_t expression = ui_expression_for(state, error);
    if (show_answer_expression) {
        expression = ui_dialogue_expression_for(dialogue);
    }
    return (board_wave_175c_display_page_t){
        .title = "PET",
        .status = ui_status_for(state, error),
        .detail = has_pet_line ? dialogue->pet_line : ui_detail_for(state, error),
        .hint = has_user_line ? dialogue->user_line : ui_hint_for(state, error),
        .accent_rgb565 = ui_accent_for(state, error),
        .is_error = ui_marks_error(error),
        .expression = expression,
    };
}
