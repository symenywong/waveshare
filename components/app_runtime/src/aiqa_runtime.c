#include "aiqa_runtime.h"

#include "aiqa_config.h"
#include "aiqa_config_nvs.h"
#include "aiqa_audio_capture.h"
#include "aiqa_chat_client.h"
#include "aiqa_net_connect.h"
#include "aiqa_ptt_button.h"
#include "aiqa_provider.h"
#include "aiqa_state_machine.h"
#include "board_wave_175c.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/projdefs.h"

#include <string.h>

static const char *TAG = "aiqa_runtime";

#define AIQA_APP_QUEUE_DEPTH 16
#define AIQA_UI_QUEUE_DEPTH 8
#define AIQA_NET_QUEUE_DEPTH 2
#define AIQA_CHAT_QUEUE_DEPTH 2
#define AIQA_AUDIO_QUEUE_DEPTH 2
#define AIQA_TASK_STACK_APP 4096
#define AIQA_TASK_STACK_UI 6144
#define AIQA_TASK_STACK_WORKER 4096
#define AIQA_TASK_STACK_NET 6144
#define AIQA_TASK_STACK_CHAT 8192
#define AIQA_TASK_STACK_BUTTON 3072

static const char *AIQA_FIXED_TEXT_PROMPT =
    "Introduce yourself as a tiny AI electronic pet and explain why rain happens in one short answer.";

typedef struct {
    aiqa_state_t state;
    aiqa_error_code_t error;
} aiqa_ui_message_t;

typedef enum {
    AIQA_NET_COMMAND_CONNECT = 0,
} aiqa_net_command_type_t;

typedef struct {
    aiqa_net_command_type_t type;
} aiqa_net_command_t;

typedef enum {
    AIQA_CHAT_COMMAND_FIXED_PROMPT = 0,
} aiqa_chat_command_type_t;

typedef struct {
    aiqa_chat_command_type_t type;
    const char *prompt;
} aiqa_chat_command_t;

typedef enum {
    AIQA_AUDIO_COMMAND_START_RECORDING = 0,
    AIQA_AUDIO_COMMAND_STOP_RECORDING,
} aiqa_audio_command_type_t;

typedef struct {
    aiqa_audio_command_type_t type;
    uint32_t max_record_ms;
} aiqa_audio_command_t;

static QueueHandle_t s_app_queue;
static QueueHandle_t s_ui_queue;
static QueueHandle_t s_net_queue;
static QueueHandle_t s_chat_queue;
static QueueHandle_t s_audio_queue;
static aiqa_config_snapshot_t s_config_snapshot;
static bool s_config_snapshot_ready;
static bool s_fixed_prompt_sent;

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

static void post_ui_state(aiqa_state_t state, aiqa_error_code_t error)
{
    if (s_ui_queue == NULL) {
        return;
    }

    aiqa_ui_message_t message = {
        .state = state,
        .error = error,
    };
    (void)xQueueSend(s_ui_queue, &message, 0);
}

static const char *ui_status_for_message(aiqa_ui_message_t message)
{
    if (message.error != AIQA_ERROR_NONE) {
        switch (message.error) {
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

    switch (message.state) {
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

static const char *ui_detail_for_message(aiqa_ui_message_t message)
{
    if (message.error != AIQA_ERROR_NONE) {
        switch (message.error) {
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

    switch (message.state) {
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

static const char *ui_hint_for_message(aiqa_ui_message_t message)
{
    if (message.error == AIQA_ERROR_CONFIG_MISSING) {
        return "RUN PROVISION TOOL";
    }
    if (message.error != AIQA_ERROR_NONE) {
        return "SEE USB SERIAL LOG";
    }

    switch (message.state) {
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

static uint16_t ui_accent_for_message(aiqa_ui_message_t message)
{
    if (message.error != AIQA_ERROR_NONE) {
        return 0xFD20;
    }

    switch (message.state) {
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

static bool ui_is_error_message(aiqa_ui_message_t message)
{
    return message.error != AIQA_ERROR_NONE && message.error != AIQA_ERROR_CONFIG_MISSING;
}

static board_wave_175c_pet_expression_t ui_expression_for_message(aiqa_ui_message_t message)
{
    if (message.error != AIQA_ERROR_NONE) {
        return message.error == AIQA_ERROR_CONFIG_MISSING
                   ? BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS
                   : BOARD_WAVE_175C_PET_EXPRESSION_WORRIED;
    }

    switch (message.state) {
    case AIQA_STATE_BOOT:
        return BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY;
    case AIQA_STATE_CONFIG_CHECK:
        return BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS;
    case AIQA_STATE_NETWORK_CONNECTING:
    case AIQA_STATE_TRANSCRIBING:
    case AIQA_STATE_ASR_JOB_PENDING:
    case AIQA_STATE_THINKING:
        return BOARD_WAVE_175C_PET_EXPRESSION_THINKING;
    case AIQA_STATE_RECORDING:
        return BOARD_WAVE_175C_PET_EXPRESSION_LISTENING;
    case AIQA_STATE_IDLE:
    case AIQA_STATE_IDLE_WITH_RESULT:
        return BOARD_WAVE_175C_PET_EXPRESSION_HAPPY;
    default:
    return BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS;
    }
}

static void handle_recording_transition(aiqa_transition_t transition)
{
    if (!transition.accepted || !transition.changed) {
        return;
    }

    esp_err_t ret = ESP_OK;
    if (transition.next_state == AIQA_STATE_RECORDING) {
        aiqa_ptt_policy_t policy = aiqa_ptt_default_policy();
        ret = post_audio_command((aiqa_audio_command_t){
            .type = AIQA_AUDIO_COMMAND_START_RECORDING,
            .max_record_ms = policy.max_record_ms,
        });
    } else if (transition.previous_state == AIQA_STATE_RECORDING) {
        ret = post_audio_command((aiqa_audio_command_t){
            .type = AIQA_AUDIO_COMMAND_STOP_RECORDING,
            .max_record_ms = 0,
        });
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post audio command: %s", esp_err_to_name(ret));
    }
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
        } else if (event.type == AIQA_EVENT_NETWORK_READY && !s_fixed_prompt_sent) {
            s_fixed_prompt_sent = true;
            aiqa_chat_command_t command = {
                .type = AIQA_CHAT_COMMAND_FIXED_PROMPT,
                .prompt = AIQA_FIXED_TEXT_PROMPT,
            };
            esp_err_t chat_ret = post_chat_command(command);
            if (chat_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to post chat command: %s", esp_err_to_name(chat_ret));
                aiqa_event_t failed = {
                    .type = AIQA_EVENT_CHAT_FAILED,
                    .error = AIQA_ERROR_CHAT_FAILED,
                    .value = (uint32_t)chat_ret,
                };
                post_ret = aiqa_runtime_post_event(failed);
                if (post_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to post chat failure event: %s", esp_err_to_name(post_ret));
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
        const board_wave_175c_display_page_t boot_page = {
            .title = "AI PET",
            .status = "WAKING UP",
            .detail = "PET IS WAKING",
            .hint = "WAIT RUNTIME",
            .accent_rgb565 = 0x001F,
            .is_error = false,
            .expression = BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY,
        };
        display_ret = board_wave_175c_display_show_page(&boot_page);
        if (display_ret != ESP_OK) {
            ESP_LOGW("aiqa_ui", "LCD boot page failed: %s", esp_err_to_name(display_ret));
        }
    } else {
        ESP_LOGW("aiqa_ui", "LCD init failed; serial UI remains active: %s", esp_err_to_name(display_ret));
    }

    while (true) {
        aiqa_ui_message_t message;
        if (xQueueReceive(s_ui_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (message.error == AIQA_ERROR_NONE) {
            ESP_LOGI("aiqa_ui", "UI state: %s", aiqa_state_name(message.state));
        } else {
            ESP_LOGW("aiqa_ui", "UI state: %s error=%s",
                     aiqa_state_name(message.state),
                     aiqa_error_name(message.error));
        }

        if (display_ready) {
            const board_wave_175c_display_page_t page = {
                .title = "AI PET",
                .status = ui_status_for_message(message),
                .detail = ui_detail_for_message(message),
                .hint = ui_hint_for_message(message),
                .accent_rgb565 = ui_accent_for_message(message),
                .is_error = ui_is_error_message(message),
                .expression = ui_expression_for_message(message),
            };
            display_ret = board_wave_175c_display_show_page(&page);
            if (display_ret != ESP_OK) {
                ESP_LOGW("aiqa_ui", "LCD page draw failed: %s", esp_err_to_name(display_ret));
                display_ready = false;
            }
        }
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    ESP_LOGI("aiqa_audio", "Audio task ready: PTT session owner, ES7210 DMA still staged");
    bool recording = false;
    while (true) {
        aiqa_audio_command_t command;
        if (xQueueReceive(s_audio_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (command.type) {
        case AIQA_AUDIO_COMMAND_START_RECORDING:
            if (!recording) {
                recording = true;
                ESP_LOGI("aiqa_audio", "PTT recording session started: max=%u ms, PCM driver staged",
                         (unsigned)command.max_record_ms);
            }
            break;
        case AIQA_AUDIO_COMMAND_STOP_RECORDING:
            if (recording) {
                recording = false;
                ESP_LOGI("aiqa_audio", "PTT recording session stopped; ASR input capture not yet enabled");
            }
            break;
        default:
            break;
        }
    }
}

static const char *ptt_output_name(aiqa_ptt_output_t output)
{
    switch (output) {
    case AIQA_PTT_OUTPUT_PRESS_START:
        return "PRESS_START";
    case AIQA_PTT_OUTPUT_PRESS_END:
        return "PRESS_END";
    case AIQA_PTT_OUTPUT_AUDIO_TOO_LONG:
        return "AUDIO_TOO_LONG";
    case AIQA_PTT_OUTPUT_NONE:
    default:
        return "NONE";
    }
}

static bool ptt_output_to_event(aiqa_ptt_output_t output, aiqa_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    switch (output) {
    case AIQA_PTT_OUTPUT_PRESS_START:
        *event = (aiqa_event_t){
            .type = AIQA_EVENT_PRESS_START,
            .error = AIQA_ERROR_NONE,
            .value = 0,
        };
        return true;
    case AIQA_PTT_OUTPUT_PRESS_END:
        *event = (aiqa_event_t){
            .type = AIQA_EVENT_PRESS_END,
            .error = AIQA_ERROR_NONE,
            .value = 0,
        };
        return true;
    case AIQA_PTT_OUTPUT_AUDIO_TOO_LONG:
        *event = (aiqa_event_t){
            .type = AIQA_EVENT_AUDIO_TOO_LONG,
            .error = AIQA_ERROR_AUDIO_TOO_LONG,
            .value = 0,
        };
        return true;
    case AIQA_PTT_OUTPUT_NONE:
    default:
        return false;
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
            if (ptt_output_to_event(output, &event)) {
                ESP_LOGI("aiqa_button", "PTT output: %s", ptt_output_name(output));
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

static aiqa_event_t chat_result_to_event(const aiqa_chat_result_t *result, esp_err_t transport_ret)
{
    aiqa_chat_status_t status = result != NULL ? result->status : AIQA_CHAT_ERR_PROVIDER;
    if (transport_ret == ESP_ERR_TIMEOUT) {
        status = AIQA_CHAT_ERR_TIMEOUT;
    }

    switch (status) {
    case AIQA_CHAT_OK:
        return (aiqa_event_t){
            .type = AIQA_EVENT_CHAT_DONE,
            .error = AIQA_ERROR_NONE,
            .value = 0,
        };
    case AIQA_CHAT_ERR_AUTH:
        return (aiqa_event_t){
            .type = AIQA_EVENT_AUTH_FAILED,
            .error = AIQA_ERROR_AUTH_FAILED,
            .value = (uint32_t)status,
        };
    case AIQA_CHAT_ERR_RATE_LIMITED:
        return (aiqa_event_t){
            .type = AIQA_EVENT_RATE_LIMITED,
            .error = AIQA_ERROR_RATE_LIMITED,
            .value = (uint32_t)status,
        };
    case AIQA_CHAT_ERR_TIMEOUT:
        return (aiqa_event_t){
            .type = AIQA_EVENT_TIMEOUT,
            .error = AIQA_ERROR_TIMEOUT,
            .value = (uint32_t)status,
        };
    case AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER:
        return (aiqa_event_t){
            .type = AIQA_EVENT_PROVIDER_UNSUPPORTED,
            .error = AIQA_ERROR_PROVIDER_UNSUPPORTED,
            .value = (uint32_t)status,
        };
    default:
        return (aiqa_event_t){
            .type = AIQA_EVENT_CHAT_FAILED,
            .error = AIQA_ERROR_CHAT_FAILED,
            .value = (uint32_t)status,
        };
    }
}

static void chat_task(void *arg)
{
    (void)arg;
    ESP_LOGI("aiqa_chat", "Chat task ready: fixed prompt Qwen bring-up");
    while (true) {
        aiqa_chat_command_t command;
        if (xQueueReceive(s_chat_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.type != AIQA_CHAT_COMMAND_FIXED_PROMPT || command.prompt == NULL) {
            continue;
        }
        if (!s_config_snapshot_ready) {
            ESP_LOGE("aiqa_chat", "Chat requested before config snapshot was loaded");
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_CONFIG_CORRUPT,
                .error = AIQA_ERROR_CONFIG_CORRUPT,
                .value = 0,
            });
            continue;
        }

        (void)aiqa_runtime_post_event((aiqa_event_t){
            .type = AIQA_EVENT_CHAT_STARTED,
            .error = AIQA_ERROR_NONE,
            .value = 0,
        });

        aiqa_chat_result_t result = {0};
        esp_err_t chat_ret = aiqa_chat_send_once(
            &s_config_snapshot.config,
            &s_config_snapshot.secrets,
            command.prompt,
            &result);
        if (result.status == AIQA_CHAT_OK) {
            ESP_LOGI("aiqa_chat", "Chat answer ready: %u bytes", (unsigned)strlen(result.text));
        }

        aiqa_event_t event = chat_result_to_event(&result, chat_ret);
        esp_err_t post_ret = aiqa_runtime_post_event(event);
        if (post_ret != ESP_OK) {
            ESP_LOGE("aiqa_chat", "Failed to post chat result: %s", esp_err_to_name(post_ret));
        }
    }
}

esp_err_t aiqa_runtime_start(void)
{
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
    if (s_app_queue == NULL || s_ui_queue == NULL || s_net_queue == NULL ||
        s_chat_queue == NULL || s_audio_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(app_state_task, "aiqa_state", AIQA_TASK_STACK_APP, NULL, 8, NULL) != pdPASS ||
        xTaskCreate(ui_task, "aiqa_ui", AIQA_TASK_STACK_UI, NULL, 6, NULL) != pdPASS ||
        xTaskCreate(audio_task, "aiqa_audio", AIQA_TASK_STACK_WORKER, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(button_task, "aiqa_button", AIQA_TASK_STACK_BUTTON, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(net_task, "aiqa_net", AIQA_TASK_STACK_NET, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(chat_task, "aiqa_chat", AIQA_TASK_STACK_CHAT, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
