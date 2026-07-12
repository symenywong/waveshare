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

static const char *ui_status_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (error != AIQA_ERROR_NONE) {
        switch (error) {
        case AIQA_ERROR_CONFIG_MISSING:
            return "SETUP NEEDED";
        case AIQA_ERROR_CONFIG_CORRUPT:
            return "CONFIG ERROR";
        case AIQA_ERROR_NETWORK_FAILED:
            return "NETWORK FAILED";
        case AIQA_ERROR_AUTH_FAILED:
            return "AUTH FAILED";
        case AIQA_ERROR_TLS_FAILED:
            return "TLS FAILED";
        case AIQA_ERROR_CERT_TIME_INVALID:
            return "TIME INVALID";
        case AIQA_ERROR_RATE_LIMITED:
            return "RATE LIMITED";
        case AIQA_ERROR_AUDIO_TOO_LONG:
            return "AUDIO TOO LONG";
        case AIQA_ERROR_ASR_FAILED:
            return "ASR FAILED";
        case AIQA_ERROR_CHAT_FAILED:
            return "CHAT FAILED";
        case AIQA_ERROR_TIMEOUT:
            return "TIMEOUT";
        default:
            return "ERROR";
        }
    }

    switch (state) {
    case AIQA_STATE_BOOT:
        return "WAKING UP";
    case AIQA_STATE_CONFIG_CHECK:
        return "CONFIG CHECK";
    case AIQA_STATE_NETWORK_CONNECTING:
        return "CONNECTING";
    case AIQA_STATE_IDLE:
        return "READY";
    case AIQA_STATE_IDLE_WITH_RESULT:
        return "ANSWER READY";
    case AIQA_STATE_RECORDING:
        return "LISTENING";
    case AIQA_STATE_TRANSCRIBING:
        return "TRANSCRIBING";
    case AIQA_STATE_ASR_JOB_PENDING:
        return "ASR PENDING";
    case AIQA_STATE_THINKING:
        return "THINKING";
    default:
        return "UNKNOWN";
    }
}

static const char *ui_detail_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (error != AIQA_ERROR_NONE) {
        switch (error) {
        case AIQA_ERROR_CONFIG_MISSING:
            return "NVS CONFIG MISSING";
        case AIQA_ERROR_CONFIG_CORRUPT:
            return "CHECK PROVISION DATA";
        case AIQA_ERROR_NETWORK_FAILED:
            return "CHECK WIFI OR TIME";
        case AIQA_ERROR_AUTH_FAILED:
            return "CHECK MODEL API KEY";
        case AIQA_ERROR_TLS_FAILED:
        case AIQA_ERROR_CERT_TIME_INVALID:
            return "CHECK TLS CLOCK";
        case AIQA_ERROR_RATE_LIMITED:
            return "MODEL RATE LIMITED";
        case AIQA_ERROR_AUDIO_TOO_LONG:
            return "RECORDING TOO LONG";
        case AIQA_ERROR_ASR_FAILED:
            return "SPEECH JOB FAILED";
        case AIQA_ERROR_CHAT_FAILED:
            return "CHAT REQUEST FAILED";
        case AIQA_ERROR_TIMEOUT:
            return "OPERATION TIMEOUT";
        default:
            return "SEE SERIAL LOG";
        }
    }

    switch (state) {
    case AIQA_STATE_BOOT:
        return "PET IS WAKING";
    case AIQA_STATE_CONFIG_CHECK:
        return "READING DEVICE CONFIG";
    case AIQA_STATE_NETWORK_CONNECTING:
        return "JOINING WIFI";
    case AIQA_STATE_IDLE:
        return "HOLD BUTTON TO CHAT";
    case AIQA_STATE_RECORDING:
        return "SPEAK NOW";
    case AIQA_STATE_TRANSCRIBING:
    case AIQA_STATE_ASR_JOB_PENDING:
        return "VOICE TO TEXT";
    case AIQA_STATE_THINKING:
        return "ASKING ONLINE MODEL";
    case AIQA_STATE_IDLE_WITH_RESULT:
        return "ANSWER COMPLETE";
    default:
        return "SYSTEM READY";
    }
}

static const char *ui_hint_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (error == AIQA_ERROR_CONFIG_MISSING) {
        return "RUN PROVISION TOOL";
    }
    if (error == AIQA_ERROR_AUDIO_TOO_LONG ||
        error == AIQA_ERROR_ASR_FAILED ||
        error == AIQA_ERROR_CHAT_FAILED ||
        error == AIQA_ERROR_TIMEOUT) {
        return "LONG PRESS RETRY";
    }
    if (error != AIQA_ERROR_NONE) {
        return "SEE USB SERIAL LOG";
    }

    switch (state) {
    case AIQA_STATE_IDLE:
    case AIQA_STATE_IDLE_WITH_RESULT:
        return "LONG PRESS BOOT";
    case AIQA_STATE_RECORDING:
        return "RELEASE TO SEND";
    case AIQA_STATE_NETWORK_CONNECTING:
        return "WAIT FOR NETWORK";
    default:
        return "AI PET COMPANION";
    }
}

static uint16_t ui_accent_for(aiqa_state_t state, aiqa_error_code_t error)
{
    if (error != AIQA_ERROR_NONE) {
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
    if (error != AIQA_ERROR_NONE) {
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
    const bool has_dialogue = error == AIQA_ERROR_NONE &&
                              dialogue != NULL &&
                              dialogue->has_dialogue &&
                              dialogue->pet_line[0] != '\0';
    const bool show_answer_dialogue = has_dialogue && state == AIQA_STATE_IDLE_WITH_RESULT;
    const bool show_stream_dialogue = has_dialogue && state == AIQA_STATE_THINKING;
    const bool show_dialogue = show_answer_dialogue || show_stream_dialogue;
    board_wave_175c_pet_expression_t expression = ui_expression_for(state, error);
    if (show_answer_dialogue) {
        expression = ui_dialogue_expression_for(dialogue);
    }
    return (board_wave_175c_display_page_t){
        .title = "AI PET",
        .status = show_stream_dialogue ? "PET TYPING" : (show_dialogue ? "PET SAYS" : ui_status_for(state, error)),
        .detail = show_dialogue ? dialogue->pet_line : ui_detail_for(state, error),
        .hint = show_dialogue && dialogue->user_line[0] != '\0'
                    ? dialogue->user_line
                    : ui_hint_for(state, error),
        .accent_rgb565 = ui_accent_for(state, error),
        .is_error = error != AIQA_ERROR_NONE && error != AIQA_ERROR_CONFIG_MISSING,
        .expression = expression,
    };
}
