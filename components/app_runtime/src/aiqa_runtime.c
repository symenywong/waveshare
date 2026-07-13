#include "aiqa_runtime.h"
#include "aiqa_config.h"
#include "aiqa_config_nvs.h"
#include "aiqa_conversation_memory.h"
#include "aiqa_audio_capture.h"
#include "aiqa_audio_capture_hw.h"
#include "aiqa_audio_playback.h"
#include "aiqa_audio_playback_hw.h"
#include "aiqa_asr_client.h"
#include "aiqa_chat_client.h"
#include "aiqa_language.h"
#include "aiqa_net_connect.h"
#include "aiqa_ptt_button.h"
#include "aiqa_provider.h"
#include "aiqa_runtime_events.h"
#include "aiqa_runtime_recording.h"
#include "aiqa_runtime_ui.h"
#include "aiqa_state_machine.h"
#include "aiqa_tts_client.h"
#include "board_wave_175c.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/projdefs.h"
#include <stdio.h>
#include <string.h>
static const char *TAG = "aiqa_runtime";
#define AIQA_APP_QUEUE_DEPTH 16
#define AIQA_UI_QUEUE_DEPTH 8
#define AIQA_NET_QUEUE_DEPTH 2
#define AIQA_CHAT_QUEUE_DEPTH 2
#define AIQA_AUDIO_QUEUE_DEPTH 2
#define AIQA_ASR_QUEUE_DEPTH 2
#define AIQA_UI_ANIMATION_INTERVAL_MS 650u
#define AIQA_TASK_STACK_APP 4096
#define AIQA_TASK_STACK_UI 6144
#define AIQA_TASK_STACK_AUDIO 6144
#define AIQA_TASK_STACK_NET 6144
#define AIQA_TASK_STACK_CHAT 12288
#define AIQA_TASK_STACK_ASR 8192
#define AIQA_TASK_STACK_BUTTON 3072
#define AIQA_RUNTIME_CHAT_PROMPT_MAX_LEN AIQA_ASR_RESPONSE_TEXT_MAX_LEN
#define AIQA_LATENCY_MAX_COMPLETION_TOKENS 256
#define AIQA_STARTUP_AUDIO_TEST_TONE_HZ 880u
#define AIQA_STARTUP_AUDIO_TEST_TONE_MS 900u
#define AIQA_TTS_PLAYBACK_QUEUE_DEPTH 16
#define AIQA_TTS_PLAYBACK_FINISH_TIMEOUT_MS 30000u
#define AIQA_TASK_STACK_TTS_PLAYBACK 4096
typedef struct {
    aiqa_state_t state;
    aiqa_error_code_t error;
    aiqa_dialogue_view_t dialogue;
} aiqa_ui_message_t;
typedef enum { AIQA_NET_COMMAND_CONNECT = 0 } aiqa_net_command_type_t;
typedef struct {
    aiqa_net_command_type_t type;
} aiqa_net_command_t;
typedef enum {
    AIQA_CHAT_COMMAND_USER_PROMPT = 0,
    AIQA_CHAT_COMMAND_LOCAL_PET_REPLY,
} aiqa_chat_command_type_t;
typedef struct {
    aiqa_chat_command_type_t type;
    uint32_t generation;
    char prompt[AIQA_RUNTIME_CHAT_PROMPT_MAX_LEN];
} aiqa_chat_command_t;
typedef struct {
    uint32_t generation;
    char answer[AIQA_CHAT_RESPONSE_TEXT_MAX_LEN];
} aiqa_chat_stream_ui_context_t;
typedef struct {
    uint8_t pcm[AIQA_AUDIO_PLAYBACK_CHUNK_BYTES];
    size_t pcm_bytes;
    bool final;
} aiqa_tts_playback_item_t;
typedef struct {
    QueueHandle_t queue;
    SemaphoreHandle_t done;
    uint32_t generation;
    esp_err_t status;
    size_t audio_bytes;
    size_t audio_chunks;
    size_t played_bytes;
} aiqa_tts_playback_context_t;
typedef enum {
    AIQA_AUDIO_COMMAND_START_RECORDING = 0,
    AIQA_AUDIO_COMMAND_STOP_RECORDING,
} aiqa_audio_command_type_t;
typedef struct {
    aiqa_audio_command_type_t type;
    uint32_t generation;
    uint32_t max_record_ms;
} aiqa_audio_command_t;
typedef enum {
    AIQA_ASR_COMMAND_STATIC_SAMPLE = 0,
    AIQA_ASR_COMMAND_PCM_WAV,
} aiqa_asr_command_type_t;
typedef struct {
    aiqa_asr_command_type_t type;
    uint32_t generation;
    const char *audio_ref;
    const uint8_t *pcm;
    size_t pcm_bytes;
    size_t pcm_capacity;
    uint32_t sample_rate_hz;
    uint16_t bits_per_sample;
    uint16_t channels;
} aiqa_asr_command_t;
static QueueHandle_t s_app_queue, s_ui_queue, s_net_queue, s_chat_queue, s_audio_queue, s_asr_queue;
static aiqa_config_snapshot_t s_config_snapshot;
static bool s_config_snapshot_ready;
static bool s_startup_audio_test_done;
static bool s_pending_local_pet_reply;
static aiqa_dialogue_language_t s_dialogue_language = AIQA_DIALOGUE_LANGUAGE_CHINESE;
static uint32_t s_interaction_generation = 1;
static char s_latest_transcript[AIQA_ASR_RESPONSE_TEXT_MAX_LEN];
static char s_local_pet_reply[AIQA_CHAT_RESPONSE_TEXT_MAX_LEN];
static aiqa_dialogue_view_t s_latest_dialogue;
static aiqa_conversation_memory_t s_conversation_memory;
static aiqa_runtime_recording_t s_recording;

static void secure_zero_bytes(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static uint32_t current_interaction_generation(void)
{
    return s_interaction_generation;
}

static bool interaction_generation_is_current(uint32_t generation)
{
    return generation != 0 && generation == s_interaction_generation;
}

static bool event_is_interaction_scoped(aiqa_event_type_t type)
{
    switch (type) {
    case AIQA_EVENT_ASR_STARTED:
    case AIQA_EVENT_ASR_JOB_SUBMITTED:
    case AIQA_EVENT_ASR_DONE:
    case AIQA_EVENT_ASR_FAILED:
    case AIQA_EVENT_CHAT_STARTED:
    case AIQA_EVENT_CHAT_TOKEN:
    case AIQA_EVENT_CHAT_DONE:
    case AIQA_EVENT_CHAT_FAILED:
    case AIQA_EVENT_AUTH_FAILED:
    case AIQA_EVENT_RATE_LIMITED:
    case AIQA_EVENT_PROVIDER_UNSUPPORTED:
    case AIQA_EVENT_TIMEOUT:
        return true;
    default:
        return false;
    }
}

static bool should_ignore_stale_interaction_event(aiqa_event_t event)
{
    return event.value != 0 &&
           event.value != current_interaction_generation() &&
           event_is_interaction_scoped(event.type);
}

static uint32_t begin_new_interaction(void)
{
    s_interaction_generation += 1;
    if (s_interaction_generation == 0) {
        s_interaction_generation = 1;
    }
    if (s_chat_queue != NULL) {
        (void)xQueueReset(s_chat_queue);
    }
    if (s_asr_queue != NULL) {
        (void)xQueueReset(s_asr_queue);
    }

    aiqa_asr_cancel_active_request();
    aiqa_chat_cancel_active_request();
    aiqa_tts_cancel_active_request();
    ESP_LOGI(TAG, "Started interaction generation %u", (unsigned)s_interaction_generation);
    return s_interaction_generation;
}

static void clear_asr_command_pcm(aiqa_asr_command_t *command)
{
    if (command == NULL || command->pcm == NULL || command->pcm_capacity == 0) {
        return;
    }
    secure_zero_bytes((void *)command->pcm, command->pcm_capacity);
    heap_caps_free((void *)command->pcm);
    command->pcm = NULL;
    command->pcm_bytes = 0;
    command->pcm_capacity = 0;
}
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS requires explicit recovery: %s", esp_err_to_name(ret));
        return ret;
    }
    return ret;
}

static aiqa_event_t load_config_event(void)
{
    aiqa_config_snapshot_t snapshot;
    esp_err_t load_ret = aiqa_config_load_from_nvs(&snapshot);
    if (load_ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS config load failed: %s", esp_err_to_name(load_ret));
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_CORRUPT,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
            .value = (uint32_t)load_ret,
        };
    }
    if (!snapshot.namespace_found) {
        ESP_LOGW(TAG, "NVS namespace 'aiqa' is missing; run provisioning before network use");
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_MISSING,
            .error = AIQA_ERROR_CONFIG_MISSING,
            .value = 0,
        };
    }
    if (snapshot.config_status != AIQA_CONFIG_OK) {
        ESP_LOGE(TAG, "Config validation failed: %s",
                 aiqa_config_status_name(snapshot.config_status));
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_CORRUPT,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
            .value = (uint32_t)snapshot.config_status,
        };
    }
    if (snapshot.secret_status != AIQA_SECRET_OK) {
        ESP_LOGE(TAG, "Secret validation failed: %s",
                 aiqa_secret_status_name(snapshot.secret_status));
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_CORRUPT,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
            .value = (uint32_t)snapshot.secret_status,
        };
    }

    if (snapshot.config.max_completion_tokens > AIQA_LATENCY_MAX_COMPLETION_TOKENS) {
        ESP_LOGI(TAG,
                 "Clamping chat max_tokens from %d to %d for voice latency",
                 snapshot.config.max_completion_tokens,
                 AIQA_LATENCY_MAX_COMPLETION_TOKENS);
        snapshot.config.max_completion_tokens = AIQA_LATENCY_MAX_COMPLETION_TOKENS;
    }

    ESP_LOGI(TAG, "Loaded provider=%s model=%s stream=%d max_tokens=%d",
             snapshot.config.active_provider,
             snapshot.config.model,
             snapshot.config.stream ? 1 : 0,
             snapshot.config.max_completion_tokens);
    ESP_LOGI(TAG, "Wi-Fi credentials and chat key are present");
    s_config_snapshot = snapshot;
    s_config_snapshot_ready = true;
    return (aiqa_event_t){
        .type = AIQA_EVENT_CONFIG_READY,
        .error = AIQA_ERROR_NONE,
        .value = 0,
    };
}

static esp_err_t post_net_command(aiqa_net_command_t command)
{
    if (s_net_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_net_queue, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t post_chat_command(aiqa_chat_command_t command)
{
    if (s_chat_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_chat_queue, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t post_chat_prompt(const char *prompt)
{
    if (prompt == NULL || prompt[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    aiqa_chat_command_t command = {
        .type = AIQA_CHAT_COMMAND_USER_PROMPT,
        .generation = current_interaction_generation(),
        .prompt = {0},
    };
    (void)snprintf(command.prompt, sizeof(command.prompt), "%s", prompt);
    return post_chat_command(command);
}

static esp_err_t post_local_pet_reply(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    aiqa_chat_command_t command = {
        .type = AIQA_CHAT_COMMAND_LOCAL_PET_REPLY,
        .generation = current_interaction_generation(),
        .prompt = {0},
    };
    (void)snprintf(command.prompt, sizeof(command.prompt), "%s", text);
    return post_chat_command(command);
}

static esp_err_t post_audio_command(aiqa_audio_command_t command)
{
    if (s_audio_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_audio_queue, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t post_asr_command(aiqa_asr_command_t command)
{
    if (s_asr_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_asr_queue, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void post_ui_state(aiqa_state_t state, aiqa_error_code_t error)
{
    if (s_ui_queue == NULL) {
        return;
    }
    aiqa_ui_message_t message = {
        .state = state,
        .error = error,
        .dialogue = s_latest_dialogue,
    };
    (void)xQueueSend(s_ui_queue, &message, 0);
}

static void maybe_run_startup_audio_test(aiqa_transition_t transition)
{
    if (s_startup_audio_test_done ||
        transition.previous_state != AIQA_STATE_NETWORK_CONNECTING ||
        transition.next_state != AIQA_STATE_IDLE) {
        return;
    }
    s_startup_audio_test_done = true;
    ESP_LOGI("aiqa_audio", "Running ES8311+PA startup playback self-test");
    esp_err_t ret = aiqa_audio_playback_hw_play_test_tone(
        AIQA_STARTUP_AUDIO_TEST_TONE_HZ,
        AIQA_STARTUP_AUDIO_TEST_TONE_MS);
    if (ret != ESP_OK) {
        ESP_LOGW("aiqa_audio", "Startup playback self-test failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI("aiqa_audio", "Startup playback self-test finished");
    ESP_LOGI("aiqa_tts", "Startup online TTS self-test skipped for lower first-chat latency");
}

static void handle_recording_transition(aiqa_transition_t transition)
{
    if (!transition.accepted || !transition.changed) {
        return;
    }
    esp_err_t ret = ESP_OK;
    if (transition.next_state == AIQA_STATE_RECORDING) {
        aiqa_ptt_policy_t policy = aiqa_ptt_default_policy();
        const uint32_t generation = begin_new_interaction();
        aiqa_dialogue_view_clear(&s_latest_dialogue);
        (void)memset(s_latest_transcript, 0, sizeof(s_latest_transcript));
        (void)memset(s_local_pet_reply, 0, sizeof(s_local_pet_reply));
        s_pending_local_pet_reply = false;
        ret = post_audio_command((aiqa_audio_command_t){
            .type = AIQA_AUDIO_COMMAND_START_RECORDING,
            .generation = generation,
            .max_record_ms = policy.max_record_ms,
        });
    } else if (transition.previous_state == AIQA_STATE_RECORDING) {
        ret = post_audio_command((aiqa_audio_command_t){
            .type = AIQA_AUDIO_COMMAND_STOP_RECORDING,
            .generation = current_interaction_generation(),
            .max_record_ms = 0,
        });
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post audio command: %s", esp_err_to_name(ret));
    }
}

static void handle_chat_prompt_transition(aiqa_transition_t transition, aiqa_event_t event)
{
    if (!transition.accepted || transition.next_state != AIQA_STATE_THINKING ||
        event.type != AIQA_EVENT_ASR_DONE) {
        return;
    }
    esp_err_t ret = ESP_OK;
    if (s_pending_local_pet_reply) {
        ret = post_local_pet_reply(s_local_pet_reply);
        s_pending_local_pet_reply = false;
        (void)memset(s_local_pet_reply, 0, sizeof(s_local_pet_reply));
    } else {
        ret = post_chat_prompt(s_latest_transcript);
    }
    (void)memset(s_latest_transcript, 0, sizeof(s_latest_transcript));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post transcript follow-up: %s", esp_err_to_name(ret));
    }
}

static bool handle_language_switch_transcript(const char *transcript)
{
    aiqa_dialogue_language_t language = s_dialogue_language;
    if (!aiqa_language_detect_switch_command(transcript, &language)) {
        return false;
    }

    s_dialogue_language = language;
    s_pending_local_pet_reply = true;
    (void)snprintf(s_local_pet_reply,
                   sizeof(s_local_pet_reply),
                   "%s",
                   aiqa_language_confirmation(language));
    aiqa_dialogue_view_set_pet(&s_latest_dialogue, aiqa_language_display_label(language));
    ESP_LOGI("aiqa_language", "Dialogue language switched to %s", aiqa_language_name(language));
    return true;
}

esp_err_t aiqa_runtime_post_event(aiqa_event_t event)
{
    if (s_app_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_app_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t aiqa_runtime_post_event_from_isr(aiqa_event_t event, bool *higher_priority_task_woken)
{
    if (s_app_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t task_woken = pdFALSE;
    if (xQueueSendFromISR(s_app_queue, &event, &task_woken) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (higher_priority_task_woken != NULL) {
        *higher_priority_task_woken = task_woken == pdTRUE;
    }
    return ESP_OK;
}

static void app_state_task(void *arg)
{
    (void)arg;
    aiqa_state_machine_t machine;
    aiqa_state_machine_init(&machine);
    post_ui_state(machine.state, machine.last_error);

    aiqa_event_t booted = {
        .type = AIQA_EVENT_BOOTED,
        .error = AIQA_ERROR_NONE,
        .value = 0,
    };
    esp_err_t post_ret = aiqa_runtime_post_event(booted);
    if (post_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post boot event: %s", esp_err_to_name(post_ret));
    }
    while (true) {
        aiqa_event_t event;
        if (xQueueReceive(s_app_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (should_ignore_stale_interaction_event(event)) {
            ESP_LOGI(TAG,
                     "Ignored stale %s from generation %u; current=%u",
                     aiqa_event_name(event.type),
                     (unsigned)event.value,
                     (unsigned)current_interaction_generation());
            continue;
        }
        aiqa_transition_t transition = aiqa_state_machine_dispatch(&machine, event);
        if (!transition.accepted) {
            ESP_LOGW(TAG, "Ignored event %s in %s",
                     aiqa_event_name(event.type),
                     aiqa_state_name(transition.previous_state));
            continue;
        }

        if (transition.changed) {
            ESP_LOGI(TAG, "%s --%s--> %s",
                     aiqa_state_name(transition.previous_state),
                     aiqa_event_name(event.type),
                     aiqa_state_name(transition.next_state));
            post_ui_state(transition.next_state, transition.error);
            handle_recording_transition(transition);
            handle_chat_prompt_transition(transition, event);
            maybe_run_startup_audio_test(transition);
        } else if (event.type == AIQA_EVENT_CHAT_TOKEN) {
            post_ui_state(transition.next_state, transition.error);
        }
        if (event.type == AIQA_EVENT_FACTORY_RESET) {
            esp_err_t erase_ret = aiqa_config_erase_nvs_namespace();
            if (erase_ret != ESP_OK) {
                ESP_LOGE(TAG, "Factory reset failed: %s", esp_err_to_name(erase_ret));
            } else {
                ESP_LOGW(TAG, "Factory reset erased NVS namespace 'aiqa'");
            }
        }
        if (transition.next_state == AIQA_STATE_CONFIG_CHECK) {
            aiqa_event_t next = load_config_event();
            post_ret = aiqa_runtime_post_event(next);
            if (post_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to post config event: %s", esp_err_to_name(post_ret));
            }
        } else if (transition.next_state == AIQA_STATE_NETWORK_CONNECTING) {
            if (!s_config_snapshot_ready) {
                ESP_LOGE(TAG, "Network requested before config snapshot was loaded");
                aiqa_event_t failed = {
                    .type = AIQA_EVENT_CONFIG_CORRUPT,
                    .error = AIQA_ERROR_CONFIG_CORRUPT,
                    .value = 0,
                };
                post_ret = aiqa_runtime_post_event(failed);
                if (post_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to post config failure event: %s", esp_err_to_name(post_ret));
                }
            } else {
                aiqa_net_command_t command = {
                    .type = AIQA_NET_COMMAND_CONNECT,
                };
                esp_err_t net_ret = post_net_command(command);
                if (net_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to post network command: %s", esp_err_to_name(net_ret));
                    aiqa_event_t failed = {
                        .type = AIQA_EVENT_NETWORK_FAILED,
                        .error = AIQA_ERROR_NETWORK_FAILED,
                        .value = (uint32_t)net_ret,
                    };
                    post_ret = aiqa_runtime_post_event(failed);
                    if (post_ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to post network failure event: %s", esp_err_to_name(post_ret));
                    }
                }
            }
        }
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    bool display_ready = false;
    esp_err_t display_ret = board_wave_175c_display_init();
    if (display_ret == ESP_OK) {
        display_ready = true;
        const board_wave_175c_display_page_t boot_page =
            aiqa_runtime_ui_page_for(AIQA_STATE_BOOT, AIQA_ERROR_NONE);
        display_ret = board_wave_175c_display_show_page(&boot_page);
        if (display_ret != ESP_OK) {
            ESP_LOGW("aiqa_ui", "LCD boot page failed: %s", esp_err_to_name(display_ret));
        }
    } else {
        ESP_LOGW("aiqa_ui", "LCD init failed; serial UI remains active: %s", esp_err_to_name(display_ret));
    }
    bool have_last_message = false;
    bool animation_warning_reported = false;
    aiqa_ui_message_t last_message = {0};
    while (true) {
        aiqa_ui_message_t message;
        const bool received =
            xQueueReceive(s_ui_queue, &message, pdMS_TO_TICKS(AIQA_UI_ANIMATION_INTERVAL_MS)) == pdTRUE;
        if (received) {
            last_message = message;
            have_last_message = true;
            animation_warning_reported = false;
            if (message.error == AIQA_ERROR_NONE) {
                ESP_LOGI("aiqa_ui", "UI state: %s", aiqa_state_name(message.state));
            } else {
                ESP_LOGW("aiqa_ui", "UI state: %s error=%s",
                         aiqa_state_name(message.state),
                         aiqa_error_name(message.error));
            }
        } else if (display_ready && have_last_message) {
            message = last_message;
        } else {
            continue;
        }
        if (display_ready) {
            const board_wave_175c_display_page_t page =
                aiqa_runtime_ui_page_for_dialogue(message.state, message.error, &message.dialogue);
            display_ret = received
                              ? board_wave_175c_display_show_page(&page)
                              : board_wave_175c_display_animate_pet(page.expression, page.accent_rgb565);
            if (display_ret != ESP_OK) {
                if (received || !animation_warning_reported) {
                    ESP_LOGW("aiqa_ui", "LCD %s draw failed: %s",
                             received ? "page" : "pet animation",
                             esp_err_to_name(display_ret));
                    animation_warning_reported = !received;
                }
            } else if (!received) {
                animation_warning_reported = false;
            }
        }
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    ESP_LOGI("aiqa_audio", "Audio task ready: ES7210 capture owner");
    const aiqa_audio_capture_hw_config_t config = aiqa_audio_capture_hw_default_config();
    const aiqa_audio_capture_config_t capture_config = aiqa_audio_capture_default_config();
    int16_t samples[AIQA_AUDIO_CAPTURE_CHUNK_FRAMES];
    while (true) {
        aiqa_audio_command_t command;
        if (xQueueReceive(s_audio_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.type != AIQA_AUDIO_COMMAND_START_RECORDING) {
            continue;
        }
        if (!interaction_generation_is_current(command.generation)) {
            ESP_LOGI("aiqa_audio", "Skipping stale recording command generation=%u",
                     (unsigned)command.generation);
            continue;
        }
        esp_err_t ret = aiqa_runtime_recording_init(&s_recording, capture_config.max_pcm_bytes);
        if (ret != ESP_OK) {
            ESP_LOGE("aiqa_audio", "Recording buffer allocation failed: %s", esp_err_to_name(ret));
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_ASR_FAILED,
                .error = AIQA_ERROR_ASR_FAILED,
                .value = (uint32_t)ret,
            });
            continue;
        }
        ret = aiqa_audio_capture_hw_init(&config);
        if (ret == ESP_OK) {
            ret = aiqa_audio_capture_hw_start();
        }
        if (ret != ESP_OK) {
            ESP_LOGE("aiqa_audio", "ES7210 recording start failed: %s", esp_err_to_name(ret));
            aiqa_runtime_recording_reset(&s_recording);
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_ASR_FAILED,
                .error = AIQA_ERROR_ASR_FAILED,
                .value = (uint32_t)ret,
            });
            continue;
        }
        bool recording = true;
        bool buffer_overflow = false;
        size_t total_bytes = 0, total_samples = 0;
        int16_t session_peak = 0;
        const uint32_t started_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ESP_LOGI("aiqa_audio", "PTT recording session started: max=%u ms", (unsigned)command.max_record_ms);
        while (recording) {
            aiqa_audio_capture_stats_t stats = {0};
            ret = aiqa_audio_capture_hw_read_mono(samples, config.chunk_frames, &stats);
            if (ret == ESP_OK) {
                total_bytes += stats.bytes_read;
                total_samples += stats.mono_samples;
                if (stats.peak_abs > session_peak) {
                    session_peak = stats.peak_abs;
                }
                esp_err_t append_ret =
                    aiqa_runtime_recording_append_i16(&s_recording, samples, stats.mono_samples);
                if (append_ret != ESP_OK) {
                    buffer_overflow = true;
                    recording = false;
                }
            } else {
                ESP_LOGW("aiqa_audio", "ES7210 read failed: %s", esp_err_to_name(ret));
            }
            const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (command.max_record_ms > 0 && now_ms - started_ms >= command.max_record_ms) {
                recording = false;
            }
            aiqa_audio_command_t pending = {0};
            if (xQueueReceive(s_audio_queue, &pending, 0) == pdTRUE &&
                pending.type == AIQA_AUDIO_COMMAND_STOP_RECORDING &&
                pending.generation == command.generation) {
                recording = false;
            }
        }
        (void)aiqa_audio_capture_hw_stop();
        (void)memset(samples, 0, sizeof(samples));
        ESP_LOGI("aiqa_audio", "PTT recording stopped: bytes=%u mono_samples=%u pcm_bytes=%u peak=%d",
                 (unsigned)total_bytes,
                 (unsigned)total_samples,
                 (unsigned)s_recording.length,
                 (int)session_peak);
        if (buffer_overflow || !aiqa_runtime_recording_has_audio(&s_recording)) {
            aiqa_runtime_recording_reset(&s_recording);
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = buffer_overflow ? AIQA_EVENT_AUDIO_TOO_LONG : AIQA_EVENT_ASR_FAILED,
                .error = buffer_overflow ? AIQA_ERROR_AUDIO_TOO_LONG : AIQA_ERROR_ASR_FAILED,
                .value = command.generation,
            });
            continue;
        }
        aiqa_asr_command_t asr_command = {
            .type = AIQA_ASR_COMMAND_PCM_WAV,
            .generation = command.generation,
            .pcm = s_recording.pcm,
            .pcm_bytes = s_recording.length,
            .pcm_capacity = s_recording.capacity,
            .sample_rate_hz = capture_config.sample_rate_hz,
            .bits_per_sample = capture_config.bits_per_sample,
            .channels = capture_config.channels,
        };
        s_recording.pcm = NULL;
        s_recording.capacity = 0;
        s_recording.length = 0;
        ret = post_asr_command(asr_command);
        if (ret != ESP_OK) {
            ESP_LOGE("aiqa_audio", "Failed to post PCM ASR command: %s", esp_err_to_name(ret));
            clear_asr_command_pcm(&asr_command);
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_ASR_FAILED,
                .error = AIQA_ERROR_ASR_FAILED,
                .value = command.generation,
            });
        }
    }
}

static void button_task(void *arg)
{
    (void)arg;

    aiqa_ptt_policy_t policy = aiqa_ptt_default_policy();
    if (!aiqa_ptt_policy_is_safe(&policy)) {
        ESP_LOGE("aiqa_button", "Unsafe PTT policy; button task stopped");
        vTaskDelete(NULL);
        return;
    }

    aiqa_ptt_button_t button;
    aiqa_ptt_button_init(&button);
    ESP_LOGI("aiqa_button", "BOOT long-press PTT ready: threshold=%u ms max=%u ms",
             (unsigned)policy.long_press_ms,
             (unsigned)policy.max_record_ms);

    while (true) {
        bool pressed = false;
        esp_err_t ret = board_wave_175c_boot_button_is_pressed(&pressed);
        if (ret == ESP_OK) {
            uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            aiqa_ptt_output_t output = aiqa_ptt_button_update(&button, &policy, pressed, now_ms);
            aiqa_event_t event = {0};
            if (aiqa_runtime_ptt_output_to_event(output, &event)) {
                ESP_LOGI("aiqa_button", "PTT output: %s", aiqa_event_name(event.type));
                esp_err_t post_ret = aiqa_runtime_post_event(event);
                if (post_ret != ESP_OK) {
                    ESP_LOGW("aiqa_button", "Failed to post PTT event: %s", esp_err_to_name(post_ret));
                }
            }
        } else {
            ESP_LOGW("aiqa_button", "BOOT read failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(policy.poll_interval_ms));
    }
}

static void net_task(void *arg)
{
    (void)arg;
    ESP_LOGI("aiqa_net", "Net task ready: Wi-Fi + SNTP owner");
    while (true) {
        aiqa_net_command_t command;
        if (xQueueReceive(s_net_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (command.type != AIQA_NET_COMMAND_CONNECT) {
            continue;
        }

        aiqa_net_policy_t policy = aiqa_net_default_policy();
        esp_err_t ret = aiqa_net_connect_wifi_and_sync_time(&s_config_snapshot.secrets, &policy);
        aiqa_event_t event = {
            .type = ret == ESP_OK ? AIQA_EVENT_NETWORK_READY : AIQA_EVENT_NETWORK_FAILED,
            .error = ret == ESP_OK ? AIQA_ERROR_NONE : AIQA_ERROR_NETWORK_FAILED,
            .value = (uint32_t)ret,
        };
        esp_err_t post_ret = aiqa_runtime_post_event(event);
        if (post_ret != ESP_OK) {
            ESP_LOGE("aiqa_net", "Failed to post network result: %s", esp_err_to_name(post_ret));
        }
    }
}

static void asr_task(void *arg)
{
    (void)arg;
    ESP_LOGI("aiqa_asr", "ASR task ready: PCM WAV transcription owner");
    while (true) {
        aiqa_asr_command_t command;
        if (xQueueReceive(s_asr_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.type != AIQA_ASR_COMMAND_STATIC_SAMPLE && command.type != AIQA_ASR_COMMAND_PCM_WAV) {
            continue;
        }
        if ((command.type == AIQA_ASR_COMMAND_STATIC_SAMPLE && command.audio_ref == NULL) ||
            (command.type == AIQA_ASR_COMMAND_PCM_WAV &&
             (command.pcm == NULL || command.pcm_bytes == 0))) {
            clear_asr_command_pcm(&command);
            continue;
        }
        if (!interaction_generation_is_current(command.generation)) {
            ESP_LOGI("aiqa_asr", "Skipping stale ASR command generation=%u",
                     (unsigned)command.generation);
            clear_asr_command_pcm(&command);
            continue;
        }
        if (!s_config_snapshot_ready) {
            ESP_LOGE("aiqa_asr", "ASR requested before config snapshot was loaded");
            clear_asr_command_pcm(&command);
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_CONFIG_CORRUPT,
                .error = AIQA_ERROR_CONFIG_CORRUPT,
                .value = command.generation,
            });
            continue;
        }

        (void)aiqa_runtime_post_event((aiqa_event_t){
            .type = AIQA_EVENT_ASR_STARTED,
            .error = AIQA_ERROR_NONE,
            .value = command.generation,
        });

        aiqa_asr_result_t result = {0};
        esp_err_t asr_ret = ESP_OK;
        if (command.type == AIQA_ASR_COMMAND_PCM_WAV) {
            const aiqa_asr_pcm_audio_t audio = {
                .pcm = command.pcm,
                .pcm_bytes = command.pcm_bytes,
                .sample_rate_hz = command.sample_rate_hz,
                .bits_per_sample = command.bits_per_sample,
                .channels = command.channels,
            };
            asr_ret = aiqa_asr_transcribe_pcm_wav_once(
                &s_config_snapshot.config,
                &s_config_snapshot.secrets,
                &audio,
                &result);
        } else {
            asr_ret = aiqa_asr_transcribe_once(
                &s_config_snapshot.config,
                &s_config_snapshot.secrets,
                command.audio_ref,
                &result);
        }
        clear_asr_command_pcm(&command);
        if (!interaction_generation_is_current(command.generation)) {
            ESP_LOGI("aiqa_asr", "Dropping stale ASR result generation=%u",
                     (unsigned)command.generation);
            (void)memset(result.text, 0, sizeof(result.text));
            continue;
        }
        if (result.status == AIQA_ASR_OK) {
            (void)snprintf(s_latest_transcript, sizeof(s_latest_transcript), "%s", result.text);
            aiqa_dialogue_view_set_user(&s_latest_dialogue, result.text);
            (void)handle_language_switch_transcript(result.text);
            ESP_LOGI("aiqa_asr", "ASR transcript ready: %u bytes", (unsigned)strlen(result.text));
        }

        aiqa_event_t event = aiqa_runtime_asr_result_to_event(&result, asr_ret);
        event.value = command.generation;
        (void)memset(result.text, 0, sizeof(result.text));
        esp_err_t post_ret = aiqa_runtime_post_event(event);
        if (post_ret != ESP_OK) {
            ESP_LOGE("aiqa_asr", "Failed to post ASR result: %s", esp_err_to_name(post_ret));
        }
    }
}

static void chat_stream_delta_callback(const char *delta, void *user_ctx)
{
    aiqa_chat_stream_ui_context_t *context = (aiqa_chat_stream_ui_context_t *)user_ctx;
    if (context == NULL || delta == NULL || delta[0] == '\0') {
        return;
    }
    if (!interaction_generation_is_current(context->generation)) {
        return;
    }

    const size_t used = strlen(context->answer);
    const size_t delta_len = strlen(delta);
    const size_t available = sizeof(context->answer) - used - 1;
    const size_t copy_len = delta_len < available ? delta_len : available;
    if (copy_len > 0) {
        (void)memcpy(context->answer + used, delta, copy_len);
        context->answer[used + copy_len] = '\0';
    }

    aiqa_dialogue_view_t updated = s_latest_dialogue;
    aiqa_dialogue_view_set_pet(&updated, context->answer);
    if (strcmp(updated.pet_line, s_latest_dialogue.pet_line) == 0) {
        return;
    }

    s_latest_dialogue = updated;
    (void)aiqa_runtime_post_event((aiqa_event_t){
        .type = AIQA_EVENT_CHAT_TOKEN,
        .error = AIQA_ERROR_NONE,
        .value = context->generation,
    });
}

static void tts_stream_playback_task(void *arg)
{
    aiqa_tts_playback_context_t *context = (aiqa_tts_playback_context_t *)arg;
    if (context == NULL) {
        vTaskDelete(NULL);
        return;
    }

    bool started = false;
    esp_err_t playback_ret = ESP_OK;
    aiqa_audio_playback_config_t playback_config = aiqa_audio_playback_default_config();

    while (context->status == ESP_OK) {
        aiqa_tts_playback_item_t item = {0};
        if (xQueueReceive(context->queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (item.final) {
            break;
        }
        if (!interaction_generation_is_current(context->generation)) {
            playback_ret = ESP_ERR_INVALID_STATE;
            secure_zero_bytes(item.pcm, sizeof(item.pcm));
            break;
        }

        if (!started) {
            playback_ret = aiqa_audio_playback_hw_init(&playback_config);
            if (playback_ret == ESP_OK) {
                playback_ret = aiqa_audio_playback_hw_start();
            }
            if (playback_ret != ESP_OK) {
                (void)aiqa_audio_playback_hw_stop();
                secure_zero_bytes(item.pcm, sizeof(item.pcm));
                break;
            }
            started = true;
        }

        size_t offset = 0;
        while (offset < item.pcm_bytes &&
               context->status == ESP_OK &&
               interaction_generation_is_current(context->generation)) {
            size_t write_bytes = item.pcm_bytes - offset;
            if (write_bytes > AIQA_AUDIO_PLAYBACK_CHUNK_BYTES) {
                write_bytes = AIQA_AUDIO_PLAYBACK_CHUNK_BYTES;
            }
            if ((write_bytes % sizeof(int16_t)) != 0) {
                write_bytes -= write_bytes % sizeof(int16_t);
            }
            if (write_bytes == 0) {
                playback_ret = ESP_ERR_INVALID_SIZE;
                break;
            }
            playback_ret = aiqa_audio_playback_hw_write_pcm(item.pcm + offset, write_bytes);
            if (playback_ret != ESP_OK) {
                break;
            }
            offset += write_bytes;
            context->played_bytes += write_bytes;
        }
        secure_zero_bytes(item.pcm, sizeof(item.pcm));
        if (playback_ret != ESP_OK || !interaction_generation_is_current(context->generation)) {
            if (playback_ret == ESP_OK) {
                playback_ret = ESP_ERR_INVALID_STATE;
            }
            break;
        }
    }

    if (context->status == ESP_OK && playback_ret != ESP_OK) {
        context->status = playback_ret;
    }

    aiqa_tts_playback_item_t pending = {0};
    while (xQueueReceive(context->queue, &pending, 0) == pdTRUE) {
        secure_zero_bytes(pending.pcm, sizeof(pending.pcm));
    }

    if (started) {
        esp_err_t stop_ret = aiqa_audio_playback_hw_stop();
        if (playback_ret == ESP_OK) {
            playback_ret = stop_ret;
        }
        if (context->status == ESP_OK && stop_ret != ESP_OK) {
            context->status = stop_ret;
        }
    }
    xSemaphoreGive(context->done);
    vTaskDelete(NULL);
}

static bool tts_playback_audio_callback(const uint8_t *pcm, size_t pcm_bytes, void *user_ctx)
{
    aiqa_tts_playback_context_t *context = (aiqa_tts_playback_context_t *)user_ctx;
    if (context == NULL || context->status != ESP_OK || pcm == NULL || pcm_bytes == 0) {
        return false;
    }
    if (!interaction_generation_is_current(context->generation)) {
        context->status = ESP_ERR_INVALID_STATE;
        return false;
    }
    if ((pcm_bytes % sizeof(int16_t)) != 0) {
        context->status = ESP_ERR_INVALID_SIZE;
        return false;
    }

    size_t offset = 0;
    while (offset < pcm_bytes) {
        if (context->status != ESP_OK || !interaction_generation_is_current(context->generation)) {
            context->status = ESP_ERR_INVALID_STATE;
            return false;
        }
        size_t chunk_bytes = pcm_bytes - offset;
        if (chunk_bytes > AIQA_AUDIO_PLAYBACK_CHUNK_BYTES) {
            chunk_bytes = AIQA_AUDIO_PLAYBACK_CHUNK_BYTES;
        }
        if ((chunk_bytes % sizeof(int16_t)) != 0) {
            chunk_bytes -= chunk_bytes % sizeof(int16_t);
        }
        if (chunk_bytes == 0) {
            context->status = ESP_ERR_INVALID_SIZE;
            return false;
        }

        aiqa_tts_playback_item_t item = {
            .pcm = {0},
            .pcm_bytes = chunk_bytes,
            .final = false,
        };
        (void)memcpy(item.pcm, pcm + offset, chunk_bytes);
        if (xQueueSend(context->queue, &item, pdMS_TO_TICKS(1000)) != pdTRUE) {
            secure_zero_bytes(item.pcm, sizeof(item.pcm));
            context->status = ESP_ERR_TIMEOUT;
            return false;
        }
        context->audio_bytes += chunk_bytes;
        context->audio_chunks += 1;
        offset += chunk_bytes;
    }
    return true;
}

static void finish_tts_stream_playback(aiqa_tts_playback_context_t *context)
{
    if (context == NULL || context->queue == NULL || context->done == NULL) {
        return;
    }

    aiqa_tts_playback_item_t final_item = {
        .pcm = {0},
        .pcm_bytes = 0,
        .final = true,
    };
    if (xQueueSend(context->queue, &final_item, pdMS_TO_TICKS(2000)) != pdTRUE) {
        context->status = ESP_ERR_TIMEOUT;
        (void)aiqa_audio_playback_hw_stop();
    }
    if (xSemaphoreTake(context->done, pdMS_TO_TICKS(AIQA_TTS_PLAYBACK_FINISH_TIMEOUT_MS)) != pdTRUE) {
        context->status = ESP_ERR_TIMEOUT;
        (void)aiqa_audio_playback_hw_stop();
        (void)xSemaphoreTake(context->done, portMAX_DELAY);
    }
}

static void speak_pet_answer(const char *answer, uint32_t generation)
{
    if (answer == NULL || answer[0] == '\0') {
        return;
    }
    if (!interaction_generation_is_current(generation)) {
        return;
    }

    aiqa_tts_playback_context_t playback_context = {
        .queue = xQueueCreate(AIQA_TTS_PLAYBACK_QUEUE_DEPTH, sizeof(aiqa_tts_playback_item_t)),
        .done = xSemaphoreCreateBinary(),
        .generation = generation,
        .status = ESP_OK,
        .audio_bytes = 0,
        .audio_chunks = 0,
        .played_bytes = 0,
    };
    if (playback_context.queue == NULL || playback_context.done == NULL) {
        if (playback_context.queue != NULL) {
            vQueueDelete(playback_context.queue);
        }
        if (playback_context.done != NULL) {
            vSemaphoreDelete(playback_context.done);
        }
        ESP_LOGW("aiqa_tts", "TTS streaming playback queue allocation failed");
        return;
    }

    if (xTaskCreate(tts_stream_playback_task,
                    "aiqa_tts_play",
                    AIQA_TASK_STACK_TTS_PLAYBACK,
                    &playback_context,
                    6,
                    NULL) != pdPASS) {
        vQueueDelete(playback_context.queue);
        vSemaphoreDelete(playback_context.done);
        ESP_LOGW("aiqa_tts", "TTS streaming playback task creation failed");
        return;
    }

    aiqa_tts_result_t tts_result = {0};
    esp_err_t tts_ret = aiqa_tts_speak_streaming(
        &s_config_snapshot.config,
        &s_config_snapshot.secrets,
        answer,
        tts_playback_audio_callback,
        &playback_context,
        &tts_result);
    finish_tts_stream_playback(&playback_context);
    vQueueDelete(playback_context.queue);
    vSemaphoreDelete(playback_context.done);

    if (!interaction_generation_is_current(generation)) {
        return;
    }

    if (tts_ret == ESP_OK && tts_result.status == AIQA_TTS_OK &&
        playback_context.status == ESP_OK) {
        ESP_LOGI("aiqa_tts", "Pet reply streaming playback complete: chunks=%u bytes=%u played=%u",
                 (unsigned)playback_context.audio_chunks,
                 (unsigned)playback_context.audio_bytes,
                 (unsigned)playback_context.played_bytes);
        return;
    }

    ESP_LOGW("aiqa_tts", "Pet reply playback skipped/failed: transport=%s tts=%s http=%d audio_bytes=%u playback=%s chunks=%u",
             esp_err_to_name(tts_ret),
             aiqa_tts_status_name(tts_result.status),
             tts_result.http_status,
             (unsigned)tts_result.audio_bytes,
             esp_err_to_name(playback_context.status),
             (unsigned)playback_context.audio_chunks);
}

static void chat_task(void *arg)
{
    (void)arg;
    ESP_LOGI("aiqa_chat", "Chat task ready: transcript prompt bring-up");
    while (true) {
        aiqa_chat_command_t command;
        if (xQueueReceive(s_chat_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.prompt[0] == '\0') {
            continue;
        }
        if (!interaction_generation_is_current(command.generation)) {
            ESP_LOGI("aiqa_chat", "Skipping stale chat command generation=%u",
                     (unsigned)command.generation);
            (void)memset(command.prompt, 0, sizeof(command.prompt));
            continue;
        }
        if (command.type == AIQA_CHAT_COMMAND_LOCAL_PET_REPLY) {
            ESP_LOGI("aiqa_chat", "Playing local pet reply");
            speak_pet_answer(command.prompt, command.generation);
            (void)memset(command.prompt, 0, sizeof(command.prompt));
            if (interaction_generation_is_current(command.generation)) {
                (void)aiqa_runtime_post_event((aiqa_event_t){
                    .type = AIQA_EVENT_CHAT_DONE,
                    .error = AIQA_ERROR_NONE,
                    .value = command.generation,
                });
            }
            continue;
        }
        if (command.type != AIQA_CHAT_COMMAND_USER_PROMPT) {
            (void)memset(command.prompt, 0, sizeof(command.prompt));
            continue;
        }
        if (!s_config_snapshot_ready) {
            ESP_LOGE("aiqa_chat", "Chat requested before config snapshot was loaded");
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_CONFIG_CORRUPT,
                .error = AIQA_ERROR_CONFIG_CORRUPT,
                .value = command.generation,
            });
            continue;
        }

        char prompt_snapshot[AIQA_RUNTIME_CHAT_PROMPT_MAX_LEN] = {0};
        (void)snprintf(prompt_snapshot, sizeof(prompt_snapshot), "%s", command.prompt);
        char conversation_context[AIQA_CONVERSATION_MEMORY_CONTEXT_MAX_LEN] = {0};
        const bool has_conversation_context =
            aiqa_conversation_memory_build_context(
                &s_conversation_memory,
                conversation_context,
                sizeof(conversation_context));

        (void)aiqa_runtime_post_event((aiqa_event_t){
            .type = AIQA_EVENT_CHAT_STARTED,
            .error = AIQA_ERROR_NONE,
            .value = command.generation,
        });

        aiqa_chat_result_t result = {0};
        aiqa_chat_stream_ui_context_t stream_context = {
            .generation = command.generation,
            .answer = {0},
        };
        const aiqa_provider_caps_t *chat_caps =
            aiqa_provider_caps_for(s_config_snapshot.config.active_provider);
        const bool use_stream = s_config_snapshot.config.stream &&
                                chat_caps != NULL &&
                                chat_caps->supports_chat_stream;
        const char *response_language = aiqa_language_chat_code(s_dialogue_language);
        esp_err_t chat_ret = use_stream
                                 ? aiqa_chat_send_streaming_with_context(
                                       &s_config_snapshot.config,
                                       &s_config_snapshot.secrets,
                                       command.prompt,
                                       response_language,
                                       has_conversation_context ? conversation_context : NULL,
                                       chat_stream_delta_callback,
                                       &stream_context,
                                       &result)
                                 : aiqa_chat_send_once_with_context(
                                       &s_config_snapshot.config,
                                       &s_config_snapshot.secrets,
                                       command.prompt,
                                       response_language,
                                       has_conversation_context ? conversation_context : NULL,
                                       &result);
        (void)memset(command.prompt, 0, sizeof(command.prompt));
        if (!interaction_generation_is_current(command.generation)) {
            ESP_LOGI("aiqa_chat", "Dropping stale chat result generation=%u",
                     (unsigned)command.generation);
            (void)memset(result.text, 0, sizeof(result.text));
            (void)memset(prompt_snapshot, 0, sizeof(prompt_snapshot));
            (void)memset(conversation_context, 0, sizeof(conversation_context));
            continue;
        }
        if (result.status == AIQA_CHAT_OK) {
            aiqa_dialogue_view_set_pet(&s_latest_dialogue, result.text);
            if (aiqa_conversation_memory_add_turn(&s_conversation_memory, prompt_snapshot, result.text)) {
                ESP_LOGI("aiqa_memory", "Stored conversation turn: count=%u",
                         (unsigned)s_conversation_memory.count);
            }
            ESP_LOGI("aiqa_chat", "Chat answer ready: %u bytes", (unsigned)strlen(result.text));
        }

        aiqa_event_t event = aiqa_runtime_chat_result_to_event(&result, chat_ret);
        event.value = command.generation;
        esp_err_t post_ret = aiqa_runtime_post_event(event);
        if (post_ret != ESP_OK) {
            ESP_LOGE("aiqa_chat", "Failed to post chat result: %s", esp_err_to_name(post_ret));
        }
        if (result.status == AIQA_CHAT_OK) {
            speak_pet_answer(result.text, command.generation);
        }
        (void)memset(prompt_snapshot, 0, sizeof(prompt_snapshot));
        (void)memset(conversation_context, 0, sizeof(conversation_context));
        (void)memset(result.text, 0, sizeof(result.text));
    }
}

esp_err_t aiqa_runtime_start(void)
{
    s_interaction_generation = 1;
    s_dialogue_language = aiqa_language_default();
    s_pending_local_pet_reply = false;
    aiqa_conversation_memory_init(&s_conversation_memory);
    (void)memset(s_local_pet_reply, 0, sizeof(s_local_pet_reply));

    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(board_wave_175c_init_minimal(), TAG, "board init failed");
    ESP_RETURN_ON_ERROR(board_wave_175c_run_bringup_checks(), TAG, "bring-up checks failed");

    const aiqa_provider_caps_t *chat_caps = aiqa_provider_caps_for(AIQA_PROVIDER_DASHSCOPE_CHAT);
    if (chat_caps == NULL || !chat_caps->supports_chat_stream) {
        return ESP_ERR_INVALID_STATE;
    }

    aiqa_audio_capture_config_t audio_config = aiqa_audio_capture_default_config();
    if (!aiqa_audio_capture_config_is_safe(&audio_config)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Audio budget: %u Hz, %u-bit, %u channel, max %u s, max_pcm=%u bytes",
             (unsigned)audio_config.sample_rate_hz,
             (unsigned)audio_config.bits_per_sample,
             (unsigned)audio_config.channels,
             (unsigned)audio_config.max_record_seconds,
             (unsigned)audio_config.max_pcm_bytes);

    s_app_queue = xQueueCreate(AIQA_APP_QUEUE_DEPTH, sizeof(aiqa_event_t));
    s_ui_queue = xQueueCreate(AIQA_UI_QUEUE_DEPTH, sizeof(aiqa_ui_message_t));
    s_net_queue = xQueueCreate(AIQA_NET_QUEUE_DEPTH, sizeof(aiqa_net_command_t));
    s_chat_queue = xQueueCreate(AIQA_CHAT_QUEUE_DEPTH, sizeof(aiqa_chat_command_t));
    s_audio_queue = xQueueCreate(AIQA_AUDIO_QUEUE_DEPTH, sizeof(aiqa_audio_command_t));
    s_asr_queue = xQueueCreate(AIQA_ASR_QUEUE_DEPTH, sizeof(aiqa_asr_command_t));
    if (s_app_queue == NULL || s_ui_queue == NULL || s_net_queue == NULL ||
        s_chat_queue == NULL || s_audio_queue == NULL || s_asr_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(app_state_task, "aiqa_state", AIQA_TASK_STACK_APP, NULL, 8, NULL) != pdPASS ||
        xTaskCreate(ui_task, "aiqa_ui", AIQA_TASK_STACK_UI, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(audio_task, "aiqa_audio", AIQA_TASK_STACK_AUDIO, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(button_task, "aiqa_button", AIQA_TASK_STACK_BUTTON, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(net_task, "aiqa_net", AIQA_TASK_STACK_NET, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(asr_task, "aiqa_asr", AIQA_TASK_STACK_ASR, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(chat_task, "aiqa_chat", AIQA_TASK_STACK_CHAT, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
