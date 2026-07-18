#include "aiqa_runtime.h"
#include "aiqa_config.h"
#include "aiqa_config_nvs.h"
#include "aiqa_conversation_memory.h"
#include "aiqa_datetime.h"
#include "aiqa_device_intent_controller.h"
#include "aiqa_assistant_profile.h"
#include "aiqa_audio_capture.h"
#include "aiqa_audio_capture_hw.h"
#include "aiqa_audio_playback.h"
#include "aiqa_audio_playback_hw.h"
#include "aiqa_boot_gesture.h"
#include "aiqa_asr_client.h"
#include "aiqa_chat_client.h"
#include "aiqa_hardening.h"
#include "aiqa_language.h"
#include "aiqa_local_command.h"
#include "aiqa_management_service.h"
#include "aiqa_management_access.h"
#include "aiqa_pairing_esp_platform.h"
#include "aiqa_net_connect.h"
#include "aiqa_ptt_button.h"
#include "aiqa_provider.h"
#include "aiqa_runtime_events.h"
#include "aiqa_runtime_recording.h"
#include "aiqa_runtime_ui.h"
#include "aiqa_state_machine.h"
#include "aiqa_tts_client.h"
#include "aiqa_tts_playback_policy.h"
#include "board_wave_175c.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/projdefs.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
static const char *TAG = "aiqa_runtime";
#define AIQA_APP_QUEUE_DEPTH 16
#define AIQA_UI_QUEUE_DEPTH 8
#define AIQA_UI_STATE_QUEUE_DEPTH 1
#define AIQA_NET_QUEUE_DEPTH 2
#define AIQA_CHAT_QUEUE_DEPTH 2
#define AIQA_AUDIO_QUEUE_DEPTH 2
#define AIQA_ASR_QUEUE_DEPTH 2
#define AIQA_APP_EVENT_POLL_MS 100u
#define AIQA_UI_ANIMATION_INTERVAL_MS 650u
#define AIQA_CHAT_UI_UPDATE_INTERVAL_MS 150u
#define AIQA_TASK_STACK_APP 12288
#define AIQA_TASK_STACK_UI 6144
#define AIQA_TASK_STACK_AUDIO 6144
#define AIQA_TASK_STACK_NET 6144
#define AIQA_TASK_STACK_CHAT 12288
#define AIQA_TASK_STACK_ASR 16384
#define AIQA_TASK_STACK_BUTTON 3072
#define AIQA_ASR_MIN_INTERNAL_LARGEST_BLOCK_BYTES (32u * 1024u)
#define AIQA_RUNTIME_CHAT_PROMPT_MAX_LEN AIQA_ASR_RESPONSE_TEXT_MAX_LEN
#define AIQA_LATENCY_MAX_COMPLETION_TOKENS 256
#define AIQA_STARTUP_AUDIO_TEST_TONE_HZ 880u
#define AIQA_STARTUP_AUDIO_TEST_TONE_MS 900u
#define AIQA_TTS_PLAYBACK_SLOT_COUNT 96
#define AIQA_TTS_PLAYBACK_PLAY_QUEUE_DEPTH (AIQA_TTS_PLAYBACK_SLOT_COUNT + 1u)
#define AIQA_TTS_PLAYBACK_INITIAL_BUFFER_MS 500u
#define AIQA_TTS_PLAYBACK_RESUME_BUFFER_MS 340u
#define AIQA_TTS_PLAYBACK_PREBUFFER_TIMEOUT_MS 900u
#define AIQA_TTS_PLAYBACK_QUEUE_POLL_MS 10u
#define AIQA_TTS_PLAYBACK_STARVATION_THRESHOLD_MS 40u
#define AIQA_TTS_PLAYBACK_LOW_WATER_BYTES (4u * AIQA_AUDIO_PLAYBACK_CHUNK_BYTES)
#define AIQA_TTS_PLAYBACK_FINISH_TIMEOUT_MS 30000u
#define AIQA_TASK_STACK_TTS_PLAYBACK 6144
typedef enum {
    AIQA_UI_MESSAGE_STATE = 0,
    AIQA_UI_MESSAGE_PAIRING_SHOW,
    AIQA_UI_MESSAGE_PAIRING_CLEAR,
} aiqa_ui_message_type_t;
typedef struct {
    aiqa_ui_message_type_t type;
    aiqa_state_t state;
    aiqa_error_code_t error;
    aiqa_dialogue_view_t dialogue;
    char pairing_code[9];
    uint32_t command_sequence;
} aiqa_ui_message_t;
typedef enum {
    AIQA_NET_COMMAND_CONNECT = 0,
    AIQA_NET_COMMAND_DISCONNECT,
    AIQA_NET_COMMAND_APPLY_WIFI,
    AIQA_NET_COMMAND_FACTORY_RESET,
} aiqa_net_command_type_t;
typedef struct {
    aiqa_wifi_credentials_t credentials;
} aiqa_wifi_connect_job_t;
typedef struct {
    uint32_t operation_id;
    aiqa_management_owned_wifi_update_t update;
} aiqa_management_wifi_job_t;
typedef struct {
    aiqa_net_command_type_t type;
    uint32_t generation;
    aiqa_wifi_connect_job_t *connect_job;
    aiqa_management_wifi_job_t *wifi_update_job;
} aiqa_net_command_t;
typedef enum {
    AIQA_CHAT_COMMAND_USER_PROMPT = 0,
    AIQA_CHAT_COMMAND_LOCAL_PET_REPLY,
    AIQA_CHAT_COMMAND_CLEAR_DEVICE_INTENT,
} aiqa_chat_command_type_t;
typedef struct {
    aiqa_chat_command_type_t type;
    uint32_t generation;
    uint32_t request_epoch;
    uint32_t tts_request_epoch;
    char prompt[AIQA_RUNTIME_CHAT_PROMPT_MAX_LEN];
} aiqa_chat_command_t;
typedef struct {
    uint32_t generation;
    int64_t last_ui_update_us;
    char answer[AIQA_CHAT_RESPONSE_TEXT_MAX_LEN];
} aiqa_chat_stream_ui_context_t;
typedef struct {
    uint8_t pcm[AIQA_AUDIO_PLAYBACK_CHUNK_BYTES];
    size_t pcm_bytes;
} aiqa_tts_playback_slot_t;
typedef struct {
    QueueHandle_t play_queue;
    QueueHandle_t free_queue;
    SemaphoreHandle_t done;
    aiqa_tts_playback_slot_t *slots;
    uint32_t generation;
    atomic_bool producer_done;
    atomic_int status;
    atomic_size_t queued_pcm_bytes;
    size_t audio_bytes;
    size_t audio_chunks;
    size_t played_bytes;
    size_t min_queued_pcm_bytes;
    size_t low_water_hits;
    aiqa_tts_playback_starvation_stats_t starvation;
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
    uint32_t request_epoch;
    const char *audio_ref;
    const uint8_t *pcm;
    size_t pcm_bytes;
    size_t pcm_capacity;
    uint32_t sample_rate_hz;
    uint16_t bits_per_sample;
    uint16_t channels;
} aiqa_asr_command_t;
static QueueHandle_t s_app_queue, s_ui_queue, s_ui_state_queue, s_net_queue, s_chat_queue, s_audio_queue, s_asr_queue;
static QueueSetHandle_t s_ui_queue_set;
static SemaphoreHandle_t s_config_mutex;
static SemaphoreHandle_t s_interaction_commit_mutex;
static SemaphoreHandle_t s_pairing_ui_ack;
static atomic_uint_least32_t s_pairing_ui_command_sequence = ATOMIC_VAR_INIT(0);
static atomic_uint_least32_t s_pairing_ui_completed_sequence = ATOMIC_VAR_INIT(0);
static atomic_uint_least32_t s_pairing_ui_canceled_sequence = ATOMIC_VAR_INIT(0);
static atomic_bool s_pairing_ui_completed_ok = ATOMIC_VAR_INIT(false);
static aiqa_config_snapshot_t s_config_snapshot;
static bool s_config_snapshot_ready;
static aiqa_management_service_t s_management_service;
static aiqa_management_device_status_t s_management_status;
static atomic_bool s_management_service_ready = ATOMIC_VAR_INIT(false);
static atomic_bool s_runtime_ready = ATOMIC_VAR_INIT(false);
static atomic_uint_least32_t s_runtime_start_phase = ATOMIC_VAR_INIT(0U);
static atomic_int_least32_t s_runtime_start_status = ATOMIC_VAR_INIT(ESP_OK);
static bool s_startup_audio_test_done;
static bool s_pending_local_pet_reply;
static bool s_pending_local_pet_reply_visual_only;
static atomic_uint_least32_t s_interaction_generation = ATOMIC_VAR_INIT(1);
static char s_latest_transcript[AIQA_ASR_RESPONSE_TEXT_MAX_LEN];
static char s_local_pet_reply[AIQA_CHAT_RESPONSE_TEXT_MAX_LEN];
static aiqa_dialogue_view_t s_latest_dialogue;
static aiqa_conversation_memory_t s_conversation_memory;
static aiqa_device_intent_controller_t s_device_intent_controller;
static aiqa_runtime_recording_t s_recording;

static bool copy_config_snapshot(aiqa_config_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_config_mutex == NULL) {
        return false;
    }
    aiqa_config_snapshot_secure_clear(snapshot);
    if (xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    const bool ready = s_config_snapshot_ready;
    if (ready) {
        *snapshot = s_config_snapshot;
    }
    (void)xSemaphoreGive(s_config_mutex);
    return ready;
}

static void store_config_snapshot(const aiqa_config_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_config_mutex == NULL ||
        xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_config_snapshot = *snapshot;
    s_config_snapshot_ready = true;
    s_management_status.config.available = true;
    s_management_status.config.revision = snapshot->revision;
    (void)snprintf(s_management_status.config.chat_provider,
                   sizeof(s_management_status.config.chat_provider),
                   "%s", snapshot->config.active_provider);
    (void)snprintf(s_management_status.config.chat_model,
                   sizeof(s_management_status.config.chat_model),
                   "%s", snapshot->config.model);
    s_management_status.config.has_chat_api_key = snapshot->secrets.chat_api_key[0] != '\0';
    s_management_status.config.has_asr_api_key = snapshot->secrets.asr_api_key[0] != '\0';
    (void)xSemaphoreGive(s_config_mutex);
}

static void clear_config_snapshot(void)
{
    if (s_config_mutex == NULL || xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_config_snapshot_ready = false;
    aiqa_config_snapshot_secure_clear(&s_config_snapshot);
    (void)memset(&s_management_status.config, 0, sizeof(s_management_status.config));
    (void)memset(&s_management_status.wifi, 0, sizeof(s_management_status.wifi));
    (void)xSemaphoreGive(s_config_mutex);
}

static bool store_config_record(const aiqa_config_record_t *record)
{
    if (record == NULL || s_config_mutex == NULL ||
        xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    const bool ready = s_config_snapshot_ready;
    if (ready) {
        s_config_snapshot.config = record->config;
        s_config_snapshot.secrets = record->secrets;
        s_config_snapshot.config_status = AIQA_CONFIG_OK;
        s_config_snapshot.secret_status = AIQA_SECRET_OK;
        s_config_snapshot.revision = record->revision;
        s_config_snapshot.active_slot = record->active_slot;
        s_management_status.config.available = true;
        s_management_status.config.revision = record->revision;
        (void)snprintf(s_management_status.config.chat_provider,
                       sizeof(s_management_status.config.chat_provider),
                       "%s", record->config.active_provider);
        (void)snprintf(s_management_status.config.chat_model,
                       sizeof(s_management_status.config.chat_model),
                       "%s", record->config.model);
        s_management_status.config.has_chat_api_key = record->secrets.chat_api_key[0] != '\0';
        s_management_status.config.has_asr_api_key = record->secrets.asr_api_key[0] != '\0';
        s_management_status.wifi.connected = true;
    }
    (void)xSemaphoreGive(s_config_mutex);
    return ready;
}

static void publish_management_transition(
    aiqa_transition_t transition,
    aiqa_event_t event)
{
    if (s_config_mutex == NULL ||
        xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_management_status.state = transition.next_state;
    s_management_status.error = transition.error;
    if (transition.changed) {
        s_management_status.sequence += 1U;
    }
    if (event.type == AIQA_EVENT_NETWORK_READY) {
        s_management_status.wifi.connected = true;
    } else if (event.type == AIQA_EVENT_NETWORK_FAILED ||
               event.type == AIQA_EVENT_CONFIG_MISSING ||
               event.type == AIQA_EVENT_CONFIG_CORRUPT ||
               event.type == AIQA_EVENT_FACTORY_RESET) {
        s_management_status.wifi.connected = false;
        s_management_status.wifi.rssi_available = false;
    }
    (void)xSemaphoreGive(s_config_mutex);
}

static void update_snapshot_volume(uint8_t volume_percent)
{
    if (s_config_mutex == NULL || xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_config_snapshot_ready) {
        s_config_snapshot.user_prefs.volume_percent = volume_percent;
    }
    (void)xSemaphoreGive(s_config_mutex);
}

static void update_snapshot_profile(const aiqa_assistant_profile_t *profile)
{
    if (profile == NULL || s_config_mutex == NULL ||
        xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_config_snapshot_ready) {
        s_config_snapshot.user_prefs.assistant_profile = *profile;
    }
    (void)xSemaphoreGive(s_config_mutex);
}

static void update_snapshot_dialogue_language(aiqa_dialogue_language_t language)
{
    if (s_config_mutex == NULL || xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_config_snapshot_ready) {
        s_config_snapshot.user_prefs.dialogue_language = language;
    }
    (void)xSemaphoreGive(s_config_mutex);
}

static uint8_t current_volume_percent(void)
{
    aiqa_config_snapshot_t snapshot = {0};
    if (!copy_config_snapshot(&snapshot) ||
        snapshot.user_prefs.volume_percent > AIQA_AUDIO_PLAYBACK_MAX_SAFE_VOLUME_PERCENT) {
        aiqa_config_snapshot_secure_clear(&snapshot);
        return AIQA_AUDIO_PLAYBACK_DEFAULT_VOLUME_PERCENT;
    }
    const uint8_t volume = snapshot.user_prefs.volume_percent;
    aiqa_config_snapshot_secure_clear(&snapshot);
    return volume;
}

static void secure_zero_bytes(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static aiqa_wifi_connect_job_t *allocate_wifi_connect_job(
    const aiqa_secret_config_t *secrets)
{
    aiqa_wifi_connect_job_t *job = heap_caps_calloc(
        1, sizeof(*job), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (job == NULL) {
        return NULL;
    }
    if (!aiqa_wifi_credentials_from_secrets(secrets, &job->credentials)) {
        secure_zero_bytes(job, sizeof(*job));
        heap_caps_free(job);
        return NULL;
    }
    return job;
}

static void release_wifi_connect_job(aiqa_wifi_connect_job_t *job)
{
    if (job == NULL) {
        return;
    }
    aiqa_wifi_credentials_secure_clear(&job->credentials);
    heap_caps_free(job);
}

static void release_management_wifi_job(aiqa_management_wifi_job_t *job)
{
    if (job == NULL) {
        return;
    }
    aiqa_management_owned_wifi_update_secure_clear(&job->update);
    secure_zero_bytes(job, sizeof(*job));
    heap_caps_free(job);
}

static uint32_t current_interaction_generation(void)
{
    return atomic_load_explicit(&s_interaction_generation, memory_order_acquire);
}

static bool interaction_generation_is_current(uint32_t generation)
{
    return generation != 0 && generation == current_interaction_generation();
}

static bool intent_load_profile(void *context, aiqa_assistant_profile_t *out_profile)
{
    (void)context;
    aiqa_config_snapshot_t snapshot = {0};
    if (out_profile == NULL || !copy_config_snapshot(&snapshot)) {
        return false;
    }
    *out_profile = snapshot.user_prefs.assistant_profile;
    aiqa_config_snapshot_secure_clear(&snapshot);
    return true;
}

static bool intent_load_language(void *context, aiqa_dialogue_language_t *out_language)
{
    (void)context;
    aiqa_config_snapshot_t snapshot = {0};
    if (out_language == NULL || !copy_config_snapshot(&snapshot)) {
        return false;
    }
    *out_language = snapshot.user_prefs.dialogue_language;
    aiqa_config_snapshot_secure_clear(&snapshot);
    return true;
}

static esp_err_t intent_save_profile(
    void *context,
    const aiqa_assistant_profile_t *profile)
{
    (void)context;
    return aiqa_config_save_assistant_profile(profile);
}

static void intent_publish_profile(
    void *context,
    const aiqa_assistant_profile_t *profile)
{
    (void)context;
    update_snapshot_profile(profile);
}

static esp_err_t intent_save_language(
    void *context,
    aiqa_dialogue_language_t language)
{
    (void)context;
    return aiqa_config_save_dialogue_language(language);
}

static void intent_publish_language(
    void *context,
    aiqa_dialogue_language_t language)
{
    (void)context;
    update_snapshot_dialogue_language(language);
}

static bool intent_authorize_generation(void *context, uint32_t generation)
{
    (void)context;
    return interaction_generation_is_current(generation);
}

static aiqa_device_intent_ports_t device_intent_ports(void)
{
    return (aiqa_device_intent_ports_t){
        .context = NULL,
        .load_profile = intent_load_profile,
        .load_language = intent_load_language,
        .save_profile = intent_save_profile,
        .publish_profile = intent_publish_profile,
        .save_language = intent_save_language,
        .publish_language = intent_publish_language,
        .authorize_generation = intent_authorize_generation,
    };
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

static void clear_asr_command_pcm(aiqa_asr_command_t *command)
{
    if (command == NULL || command->pcm == NULL) {
        return;
    }
    if (command->pcm_capacity > 0) {
        secure_zero_bytes((void *)command->pcm, command->pcm_capacity);
    }
    heap_caps_free((void *)command->pcm);
    command->pcm = NULL;
    command->pcm_bytes = 0;
    command->pcm_capacity = 0;
}

static void clear_pending_asr_commands(void)
{
    if (s_asr_queue == NULL) {
        return;
    }

    aiqa_asr_command_t pending = {0};
    while (xQueueReceive(s_asr_queue, &pending, 0) == pdTRUE) {
        clear_asr_command_pcm(&pending);
        pending = (aiqa_asr_command_t){0};
    }
}

static uint32_t begin_new_interaction(void)
{
    if (s_interaction_commit_mutex != NULL) {
        (void)xSemaphoreTake(s_interaction_commit_mutex, portMAX_DELAY);
    }
    uint32_t generation =
        atomic_fetch_add_explicit(&s_interaction_generation, 1, memory_order_acq_rel) + 1U;
    if (generation == 0) {
        generation = 1;
        atomic_store_explicit(&s_interaction_generation, generation, memory_order_release);
    }
    if (s_interaction_commit_mutex != NULL) {
        (void)xSemaphoreGive(s_interaction_commit_mutex);
    }
    if (s_chat_queue != NULL) {
        (void)xQueueReset(s_chat_queue);
    }
    if (s_audio_queue != NULL) {
        (void)xQueueReset(s_audio_queue);
    }
    clear_pending_asr_commands();

    aiqa_asr_cancel_active_request();
    aiqa_chat_cancel_active_request();
    aiqa_tts_cancel_active_request();
    ESP_LOGI(TAG, "Started interaction generation %u", (unsigned)generation);
    return generation;
}

static bool lock_current_interaction(uint32_t generation)
{
    if (s_interaction_commit_mutex == NULL ||
        xSemaphoreTake(s_interaction_commit_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (!interaction_generation_is_current(generation)) {
        (void)xSemaphoreGive(s_interaction_commit_mutex);
        return false;
    }
    return true;
}

static void unlock_current_interaction(void)
{
    (void)xSemaphoreGive(s_interaction_commit_mutex);
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
        aiqa_config_snapshot_secure_clear(&snapshot);
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_CORRUPT,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
            .value = (uint32_t)load_ret,
        };
    }
    if (!snapshot.namespace_found) {
        ESP_LOGW(TAG, "No active device configuration; run provisioning before network use");
        aiqa_config_snapshot_secure_clear(&snapshot);
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_MISSING,
            .error = AIQA_ERROR_CONFIG_MISSING,
            .value = 0,
        };
    }
    if (snapshot.config_status != AIQA_CONFIG_OK) {
        const uint32_t status_value = (uint32_t)snapshot.config_status;
        ESP_LOGE(TAG, "Config validation failed: %s",
                 aiqa_config_status_name(snapshot.config_status));
        aiqa_config_snapshot_secure_clear(&snapshot);
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_CORRUPT,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
            .value = status_value,
        };
    }
    if (snapshot.secret_status != AIQA_SECRET_OK) {
        const uint32_t status_value = (uint32_t)snapshot.secret_status;
        ESP_LOGE(TAG, "Secret validation failed: %s",
                 aiqa_secret_status_name(snapshot.secret_status));
        aiqa_config_snapshot_secure_clear(&snapshot);
        return (aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_CORRUPT,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
            .value = status_value,
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
    ESP_LOGI(TAG, "Loaded assistant profile name=%s gender=%s language=%s volume=%u",
             snapshot.user_prefs.assistant_profile.name,
             aiqa_assistant_gender_name(snapshot.user_prefs.assistant_profile.gender),
             aiqa_language_name(snapshot.user_prefs.dialogue_language),
             (unsigned)snapshot.user_prefs.volume_percent);
    ESP_LOGI(TAG, "Wi-Fi credentials and chat key are present");
    store_config_snapshot(&snapshot);
    aiqa_config_snapshot_secure_clear(&snapshot);
    return (aiqa_event_t){
        .type = AIQA_EVENT_CONFIG_READY,
        .error = AIQA_ERROR_NONE,
        .value = 0,
    };
}

static esp_err_t post_net_command(const aiqa_net_command_t *command)
{
    if (s_net_queue == NULL || command == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_net_queue, command, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static const char *management_expression_name(board_wave_175c_pet_expression_t expression)
{
    static const char *const NAMES[] = {
        "SLEEPY", "IDLE", "HAPPY", "LISTENING", "THINKING", "SPEAKING", "SAD",
        "SHY", "FRUSTRATED", "BOUNCING", "LAUGHING", "CRYING", "CURIOUS", "WORRIED",
    };
    const unsigned index = (unsigned)expression;
    return index < BOARD_WAVE_175C_PET_EXPRESSION_COUNT
               ? NAMES[index]
               : "CURIOUS";
}

static bool management_authorize(
    void *context,
    const aiqa_management_security_context_t *access,
    aiqa_management_capability_t capability)
{
    (void)context;
    (void)capability;
    return aiqa_management_access_global_authorize(access);
}

static aiqa_management_charging_state_t management_charging_state(
    board_wave_175c_charging_state_t state)
{
    switch (state) {
    case BOARD_WAVE_175C_CHARGING_DISCHARGING:
        return AIQA_MANAGEMENT_CHARGING_DISCHARGING;
    case BOARD_WAVE_175C_CHARGING_ACTIVE:
        return AIQA_MANAGEMENT_CHARGING_ACTIVE;
    case BOARD_WAVE_175C_CHARGING_DONE:
        return AIQA_MANAGEMENT_CHARGING_DONE;
    case BOARD_WAVE_175C_CHARGING_UNKNOWN:
    default:
        return AIQA_MANAGEMENT_CHARGING_UNKNOWN;
    }
}

static bool management_copy_status(
    void *context,
    aiqa_management_device_status_t *out_status)
{
    (void)context;
    if (out_status == NULL || s_config_mutex == NULL ||
        xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    aiqa_management_device_status_t status = s_management_status;
    (void)xSemaphoreGive(s_config_mutex);

    status.uptime_ms = (uint64_t)(esp_timer_get_time() / 1000LL);
    status.free_heap_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const board_wave_175c_display_page_t page =
        aiqa_runtime_ui_page_for(status.state, status.error);
    (void)snprintf(status.ui.status, sizeof(status.ui.status), "%s", page.status);
    if (page.detail != NULL) {
        (void)snprintf(status.ui.detail, sizeof(status.ui.detail), "%s", page.detail);
    }
    if (page.hint != NULL) {
        (void)snprintf(status.ui.hint, sizeof(status.ui.hint), "%s", page.hint);
    }
    (void)snprintf(status.ui.expression,
                   sizeof(status.ui.expression),
                   "%s", management_expression_name(page.expression));

    board_wave_175c_power_status_t power = {0};
    if (board_wave_175c_get_power_status(&power) == ESP_OK) {
        status.power.battery_present = power.battery_present;
        status.power.percent_available = power.battery_present;
        status.power.percent = power.percent;
        status.power.charging_state = management_charging_state(power.charging_state);
    }
    *out_status = status;
    secure_zero_bytes(&status, sizeof(status));
    return true;
}

static bool management_copy_public_config(
    void *context,
    aiqa_management_public_config_t *out_config)
{
    (void)context;
    aiqa_config_snapshot_t snapshot = {0};
    if (out_config == NULL || !copy_config_snapshot(&snapshot)) {
        return false;
    }
    const bool projected = aiqa_management_public_config_from_snapshot(&snapshot, out_config);
    aiqa_config_snapshot_secure_clear(&snapshot);
    return projected;
}

static aiqa_management_result_t management_submit_wifi(
    void *context,
    uint32_t operation_id,
    const aiqa_management_owned_wifi_update_t *update)
{
    (void)context;
    if (operation_id == 0 || update == NULL) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    aiqa_config_snapshot_t snapshot = {0};
    if (!copy_config_snapshot(&snapshot)) {
        return AIQA_MANAGEMENT_NOT_READY;
    }
    const uint32_t active_revision = snapshot.revision;
    aiqa_config_snapshot_secure_clear(&snapshot);
    if (active_revision != update->base_revision) {
        return AIQA_MANAGEMENT_REVISION_CONFLICT;
    }
    aiqa_management_wifi_job_t *job = heap_caps_calloc(
        1, sizeof(*job), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (job == NULL) {
        return AIQA_MANAGEMENT_INTERNAL_ERROR;
    }
    job->operation_id = operation_id;
    job->update = *update;
    aiqa_net_command_t command = {
        .type = AIQA_NET_COMMAND_APPLY_WIFI,
        .wifi_update_job = job,
    };
    if (post_net_command(&command) != ESP_OK) {
        release_management_wifi_job(job);
        return AIQA_MANAGEMENT_BUSY;
    }
    return AIQA_MANAGEMENT_OK;
}

static bool complete_management_wifi_operation(
    uint32_t operation_id,
    aiqa_management_result_t result)
{
    for (uint8_t attempt = 0; attempt < 10U; ++attempt) {
        if (aiqa_management_service_complete_wifi_update(
                &s_management_service, operation_id, result)) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

static void clear_pending_net_commands(void)
{
    if (s_net_queue == NULL) {
        return;
    }
    aiqa_net_command_t pending = {0};
    while (xQueueReceive(s_net_queue, &pending, 0) == pdTRUE) {
        if (pending.connect_job != NULL) {
            release_wifi_connect_job(pending.connect_job);
        }
        if (pending.wifi_update_job != NULL) {
            (void)complete_management_wifi_operation(
                pending.wifi_update_job->operation_id,
                AIQA_MANAGEMENT_CANCELLED);
            release_management_wifi_job(pending.wifi_update_job);
        }
        secure_zero_bytes(&pending, sizeof(pending));
    }
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
        .request_epoch = aiqa_chat_request_epoch_capture(),
        .tts_request_epoch = aiqa_tts_request_epoch_capture(),
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
        .request_epoch = aiqa_chat_request_epoch_capture(),
        .tts_request_epoch = aiqa_tts_request_epoch_capture(),
        .prompt = {0},
    };
    (void)snprintf(command.prompt, sizeof(command.prompt), "%s", text);
    return post_chat_command(command);
}

static esp_err_t post_clear_device_intent(void)
{
    return post_chat_command((aiqa_chat_command_t){
        .type = AIQA_CHAT_COMMAND_CLEAR_DEVICE_INTENT,
        .generation = current_interaction_generation(),
        .prompt = {0},
    });
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
    if (s_ui_state_queue == NULL) {
        return;
    }
    aiqa_ui_message_t message = {
        .type = AIQA_UI_MESSAGE_STATE,
        .state = state,
        .error = error,
        .dialogue = s_latest_dialogue,
    };
    (void)xQueueOverwrite(s_ui_state_queue, &message);
}

static bool post_pairing_ui_command(
    aiqa_ui_message_type_t type,
    const uint8_t code[8])
{
    if (s_ui_queue == NULL || s_pairing_ui_ack == NULL ||
        (type == AIQA_UI_MESSAGE_PAIRING_SHOW && code == NULL)) {
        return false;
    }
    while (xSemaphoreTake(s_pairing_ui_ack, 0) == pdTRUE) {
    }
    uint32_t sequence = atomic_fetch_add_explicit(
                            &s_pairing_ui_command_sequence,
                            1U,
                            memory_order_relaxed) +
                        1U;
    if (sequence == 0U) {
        sequence = atomic_fetch_add_explicit(
                       &s_pairing_ui_command_sequence,
                       1U,
                       memory_order_relaxed) +
                   1U;
    }
    aiqa_ui_message_t message = {
        .type = type,
        .command_sequence = sequence,
    };
    if (code != NULL) {
        (void)memcpy(message.pairing_code, code, 8U);
        message.pairing_code[8] = '\0';
    }
    const bool queued =
        xQueueSend(s_ui_queue, &message, pdMS_TO_TICKS(100)) == pdTRUE;
    secure_zero_bytes(&message, sizeof(message));
    if (!queued) {
        return false;
    }
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(1500);
    for (;;) {
        const TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) break;
        const TickType_t remaining = timeout - elapsed;
        if (xSemaphoreTake(s_pairing_ui_ack, remaining) != pdTRUE) break;
        if (atomic_load_explicit(
                &s_pairing_ui_completed_sequence, memory_order_acquire) == sequence) {
            return atomic_load_explicit(
                &s_pairing_ui_completed_ok, memory_order_acquire);
        }
    }
    atomic_store_explicit(
        &s_pairing_ui_canceled_sequence, sequence, memory_order_release);
    return false;
}

bool aiqa_runtime_management_show_pairing_code(const uint8_t code[8])
{
    return post_pairing_ui_command(AIQA_UI_MESSAGE_PAIRING_SHOW, code);
}

bool aiqa_runtime_management_clear_pairing_code(void)
{
    return post_pairing_ui_command(AIQA_UI_MESSAGE_PAIRING_CLEAR, NULL);
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
        s_pending_local_pet_reply_visual_only = false;
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
        const aiqa_event_t failed =
            aiqa_runtime_asr_failure_event(current_interaction_generation());
        esp_err_t post_ret = aiqa_runtime_post_event(failed);
        if (post_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post audio command failure event: %s",
                     esp_err_to_name(post_ret));
        }
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
        if (s_pending_local_pet_reply_visual_only) {
            ret = aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_CHAT_DONE,
                .error = AIQA_ERROR_NONE,
                .value = event.value,
            });
        } else {
            ret = post_local_pet_reply(s_local_pet_reply);
        }
        s_pending_local_pet_reply = false;
        s_pending_local_pet_reply_visual_only = false;
        (void)memset(s_local_pet_reply, 0, sizeof(s_local_pet_reply));
    } else {
        ret = post_chat_prompt(s_latest_transcript);
    }
    (void)memset(s_latest_transcript, 0, sizeof(s_latest_transcript));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post transcript follow-up: %s", esp_err_to_name(ret));
    }
}

static void set_local_reply(const char *reply);
static void set_local_spoken_reply(const char *reply);

static bool local_time_ready(struct tm *out_tm)
{
    if (!aiqa_net_time_is_synchronized()) {
        return false;
    }
    time_t now = time(NULL);
    if (!aiqa_net_time_is_valid(now)) {
        return false;
    }
    return localtime_r(&now, out_tm) != NULL;
}

static void set_local_reply_mode(const char *reply, bool visual_only)
{
    s_pending_local_pet_reply = true;
    s_pending_local_pet_reply_visual_only = visual_only;
    (void)snprintf(s_local_pet_reply, sizeof(s_local_pet_reply), "%s", reply);
    aiqa_dialogue_view_set_pet(&s_latest_dialogue, reply);
}

static void set_local_reply(const char *reply)
{
    set_local_reply_mode(reply, true);
}

static void set_local_spoken_reply(const char *reply)
{
    set_local_reply_mode(reply, false);
}

static void handle_battery_query_reply(void)
{
    board_wave_175c_power_status_t power = {0};
    esp_err_t ret = board_wave_175c_get_power_status(&power);
    if (ret != ESP_OK) {
        set_local_reply("电量读取失败。");
        return;
    }
    if (!power.battery_present) {
        set_local_reply(power.vbus_good ? "未检测到电池，外部供电中。" : "未检测到电池。");
        return;
    }

    char reply[AIQA_CHAT_RESPONSE_TEXT_MAX_LEN] = {0};
    const char *state = "未充电";
    if (power.charging_state == BOARD_WAVE_175C_CHARGING_ACTIVE) {
        state = "正在充电";
    } else if (power.charging_state == BOARD_WAVE_175C_CHARGING_DONE) {
        state = "已充满";
    }
    (void)snprintf(reply, sizeof(reply), "电量%u%%，%s。", (unsigned)power.percent, state);
    set_local_reply(reply);
}

static bool handle_local_command_transcript(const char *transcript)
{
    aiqa_local_command_t command = {0};
    if (!aiqa_local_command_parse(transcript, &command)) {
        return false;
    }

    char reply[AIQA_CHAT_RESPONSE_TEXT_MAX_LEN] = {0};
    if (command.type == AIQA_LOCAL_COMMAND_VOLUME_SET ||
        command.type == AIQA_LOCAL_COMMAND_VOLUME_RELATIVE) {
        uint8_t next_volume = 0;
        if (command.type == AIQA_LOCAL_COMMAND_VOLUME_SET) {
            next_volume = aiqa_local_command_clamp_volume(command.value);
        } else {
            next_volume = aiqa_local_command_clamp_volume((int)current_volume_percent() + command.value);
        }
        const esp_err_t save_ret = aiqa_config_save_volume_percent(next_volume);
        if (save_ret != ESP_OK) {
            ESP_LOGE("aiqa_volume", "Failed to persist volume: %s", esp_err_to_name(save_ret));
            set_local_reply("音量设置保存失败，请重试。");
            return true;
        }
        update_snapshot_volume(next_volume);
        (void)aiqa_audio_playback_hw_set_volume(next_volume);
        (void)snprintf(reply, sizeof(reply), "音量已调到%u%%。", (unsigned)next_volume);
        set_local_reply(reply);
        return true;
    }
    if (command.type == AIQA_LOCAL_COMMAND_VOLUME_QUERY) {
        (void)snprintf(reply, sizeof(reply), "当前音量%u%%。", (unsigned)current_volume_percent());
        set_local_reply(reply);
        return true;
    }
    if (command.type == AIQA_LOCAL_COMMAND_BATTERY_QUERY) {
        handle_battery_query_reply();
        return true;
    }
    if (command.type == AIQA_LOCAL_COMMAND_DATE_QUERY ||
        command.type == AIQA_LOCAL_COMMAND_TIME_QUERY ||
        command.type == AIQA_LOCAL_COMMAND_WEEKDAY_QUERY ||
        command.type == AIQA_LOCAL_COMMAND_DATETIME_QUERY) {
        struct tm now_tm = {0};
        if (!local_time_ready(&now_tm)) {
            set_local_spoken_reply("时间还没同步，请稍后再试。");
            return true;
        }
        if (!aiqa_datetime_format_local_reply(
                command.type, &now_tm, reply, sizeof(reply))) {
            set_local_spoken_reply("时间读取失败，请稍后再试。");
            return true;
        }
        set_local_spoken_reply(reply);
        return true;
    }
    return false;
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

static esp_err_t post_critical_app_event(aiqa_event_t event)
{
    if (s_app_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_app_queue, &event, portMAX_DELAY) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
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

aiqa_management_result_t aiqa_runtime_management_get_status(
    const aiqa_management_security_context_t *access,
    aiqa_management_device_status_t *out_status)
{
    if (!atomic_load_explicit(&s_management_service_ready, memory_order_acquire)) {
        return AIQA_MANAGEMENT_NOT_READY;
    }
    return aiqa_management_service_get_status(&s_management_service, access, out_status);
}

aiqa_management_result_t aiqa_runtime_management_get_public_config(
    const aiqa_management_security_context_t *access,
    aiqa_management_public_config_t *out_config)
{
    if (!atomic_load_explicit(&s_management_service_ready, memory_order_acquire)) {
        return AIQA_MANAGEMENT_NOT_READY;
    }
    return aiqa_management_service_get_public_config(&s_management_service, access, out_config);
}

aiqa_management_result_t aiqa_runtime_management_submit_wifi_update(
    const aiqa_management_security_context_t *access,
    const aiqa_management_owned_wifi_update_t *update,
    uint32_t *out_operation_id)
{
    if (!atomic_load_explicit(&s_management_service_ready, memory_order_acquire)) {
        return AIQA_MANAGEMENT_NOT_READY;
    }
    return aiqa_management_service_submit_wifi_update(
        &s_management_service, access, update, out_operation_id);
}

static uint32_t diagnostic_size_u32(size_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

bool aiqa_runtime_management_copy_public_diagnostics(
    aiqa_management_public_diagnostics_t *out_diagnostics)
{
    if (out_diagnostics == NULL) {
        return false;
    }

    aiqa_management_device_status_t runtime_status = {0};
    if (atomic_load_explicit(&s_management_service_ready, memory_order_acquire)) {
        (void)management_copy_status(NULL, &runtime_status);
    }
    aiqa_asr_diagnostics_t asr = {0};
    if (!aiqa_asr_copy_diagnostics(&asr)) {
        secure_zero_bytes(&runtime_status, sizeof(runtime_status));
        return false;
    }

    *out_diagnostics = (aiqa_management_public_diagnostics_t){
        .runtime_ready =
            atomic_load_explicit(&s_runtime_ready, memory_order_acquire),
        .runtime_start_phase =
            atomic_load_explicit(&s_runtime_start_phase, memory_order_acquire),
        .runtime_start_status =
            atomic_load_explicit(&s_runtime_start_status, memory_order_acquire),
        .board_init_phase = board_wave_175c_init_phase(),
        .generation = current_interaction_generation(),
        .state = runtime_status.state,
        .error = runtime_status.error,
        .state_sequence = runtime_status.sequence,
        .asr = {
            .request_epoch = asr.request_epoch,
            .phase = (uint32_t)asr.phase,
            .status = (int32_t)asr.status,
            .http_status = (int32_t)asr.http_status,
            .transport_status = (int32_t)asr.transport_status,
            .socket_errno = (int32_t)asr.socket_errno,
            .content_length = asr.content_length,
            .pcm_bytes = diagnostic_size_u32(asr.pcm_bytes),
            .post_bytes = diagnostic_size_u32(asr.post_bytes),
            .uploaded_bytes = diagnostic_size_u32(asr.uploaded_bytes),
            .upload_write_calls = asr.upload_write_calls,
            .response_bytes = diagnostic_size_u32(asr.response_bytes),
            .response_limit = diagnostic_size_u32(asr.response_limit),
            .header_wait_ms = asr.header_wait_ms,
            .elapsed_ms = asr.elapsed_ms,
            .response_complete = asr.response_complete,
        },
    };
    secure_zero_bytes(&runtime_status, sizeof(runtime_status));
    return true;
}

static void app_state_task(void *arg)
{
    (void)arg;
    aiqa_state_machine_t machine;
    aiqa_state_machine_init(&machine);
    uint32_t state_entered_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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
        aiqa_event_t event = {0};
        bool watchdog_fired = false;
        const bool event_received = xQueueReceive(
                s_app_queue,
                &event,
                pdMS_TO_TICKS(AIQA_APP_EVENT_POLL_MS)) == pdTRUE;
        const uint32_t now_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        const bool watchdog_may_replace_event = !event_received ||
            event.type == AIQA_EVENT_ASR_STARTED ||
            event.type == AIQA_EVENT_ASR_JOB_SUBMITTED ||
            event.type == AIQA_EVENT_CHAT_TOKEN;
        if (aiqa_state_machine_asr_deadline_expired(
                machine.state, state_entered_ms, now_ms) &&
            watchdog_may_replace_event) {
            watchdog_fired = true;
            event = (aiqa_event_t){
                .type = AIQA_EVENT_TIMEOUT,
                .error = AIQA_ERROR_TIMEOUT,
                .value = current_interaction_generation(),
            };
            ESP_LOGE(TAG,
                     "ASR phase watchdog expired: state=%s generation=%u",
                     aiqa_state_name(machine.state),
                     (unsigned)event.value);
        } else if (!event_received) {
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
        publish_management_transition(transition, event);

        if (transition.changed) {
            const uint32_t transition_ms =
                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            const uint32_t state_elapsed_ms = transition_ms - state_entered_ms;
            state_entered_ms = transition_ms;
            ESP_LOGI(TAG, "%s --%s--> %s",
                     aiqa_state_name(transition.previous_state),
                     aiqa_event_name(event.type),
                     aiqa_state_name(transition.next_state));
            ESP_LOGI(TAG,
                     "AIQA_DIAG state generation=%u event=%s previous=%s next=%s error=%s state_elapsed_ms=%u",
                     (unsigned)current_interaction_generation(),
                     aiqa_event_name(event.type),
                     aiqa_state_name(transition.previous_state),
                     aiqa_state_name(transition.next_state),
                     aiqa_error_name(transition.error),
                     (unsigned)state_elapsed_ms);
            post_ui_state(transition.next_state, transition.error);
            handle_recording_transition(transition);
            handle_chat_prompt_transition(transition, event);
            maybe_run_startup_audio_test(transition);
        } else if (event.type == AIQA_EVENT_CHAT_TOKEN) {
            post_ui_state(transition.next_state, transition.error);
        }
        if (watchdog_fired) {
            (void)begin_new_interaction();
            (void)post_clear_device_intent();
        }
        if (event.type == AIQA_EVENT_FACTORY_RESET) {
            (void)begin_new_interaction();
            (void)post_clear_device_intent();
            if (s_net_queue != NULL) {
                clear_pending_net_commands();
                aiqa_net_command_t reset = {.type = AIQA_NET_COMMAND_FACTORY_RESET};
                esp_err_t reset_ret = post_net_command(&reset);
                if (reset_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to queue factory reset: %s", esp_err_to_name(reset_ret));
                    (void)aiqa_runtime_post_event((aiqa_event_t){
                        .type = AIQA_EVENT_CONFIG_CORRUPT,
                        .error = AIQA_ERROR_CONFIG_CORRUPT,
                        .value = (uint32_t)reset_ret,
                    });
                }
            }
            continue;
        }
        if (transition.next_state == AIQA_STATE_CONFIG_CHECK) {
            aiqa_event_t next = load_config_event();
            post_ret = aiqa_runtime_post_event(next);
            if (post_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to post config event: %s", esp_err_to_name(post_ret));
            }
        } else if (transition.next_state == AIQA_STATE_NETWORK_CONNECTING) {
            aiqa_config_snapshot_t snapshot = {0};
            if (!copy_config_snapshot(&snapshot)) {
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
                aiqa_wifi_connect_job_t *connect_job =
                    allocate_wifi_connect_job(&snapshot.secrets);
                aiqa_net_command_t command = {
                    .type = AIQA_NET_COMMAND_CONNECT,
                    .generation = current_interaction_generation(),
                    .connect_job = connect_job,
                };
                if (connect_job == NULL) {
                    aiqa_config_snapshot_secure_clear(&snapshot);
                    (void)aiqa_runtime_post_event((aiqa_event_t){
                        .type = AIQA_EVENT_CONFIG_CORRUPT,
                        .error = AIQA_ERROR_CONFIG_CORRUPT,
                    });
                    continue;
                }
                aiqa_config_snapshot_secure_clear(&snapshot);
                esp_err_t net_ret = post_net_command(&command);
                if (net_ret != ESP_OK) {
                    release_wifi_connect_job(connect_job);
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
    bool have_last_message = true;
    bool pairing_overlay = false;
    bool page_dirty = false;
    bool animation_warning_reported = false;
    aiqa_ui_message_t last_message = {
        .type = AIQA_UI_MESSAGE_STATE,
        .state = AIQA_STATE_BOOT,
        .error = AIQA_ERROR_NONE,
    };
    bool display_ready = false;
    esp_err_t display_ret = board_wave_175c_display_init();
    if (display_ret == ESP_OK) {
        display_ready = true;
        const board_wave_175c_display_page_t boot_page =
            aiqa_runtime_ui_page_for(AIQA_STATE_BOOT, AIQA_ERROR_NONE);
        display_ret = board_wave_175c_display_show_page(&boot_page);
        if (display_ret != ESP_OK) {
            ESP_LOGW("aiqa_ui", "LCD boot page failed: %s", esp_err_to_name(display_ret));
            page_dirty = true;
        }
    } else {
        ESP_LOGW("aiqa_ui", "LCD init failed; serial UI remains active: %s", esp_err_to_name(display_ret));
    }
    TickType_t last_display_init_attempt = xTaskGetTickCount();
    while (true) {
        aiqa_ui_message_t message = {0};
        const QueueSetMemberHandle_t activated = xQueueSelectFromSet(
            s_ui_queue_set,
            pdMS_TO_TICKS(AIQA_UI_ANIMATION_INTERVAL_MS));
        bool received = false;
        if (activated == s_ui_queue) {
            received = xQueueReceive(s_ui_queue, &message, 0) == pdTRUE;
        } else if (activated == s_ui_state_queue) {
            received = xQueueReceive(s_ui_state_queue, &message, 0) == pdTRUE;
        }
        if (!display_ready &&
            xTaskGetTickCount() - last_display_init_attempt >= pdMS_TO_TICKS(5000)) {
            last_display_init_attempt = xTaskGetTickCount();
            display_ret = board_wave_175c_display_init();
            if (display_ret == ESP_OK) {
                display_ready = true;
                page_dirty = true;
                ESP_LOGI("aiqa_ui", "LCD reinitialized after previous init failure");
            }
        }
        if (received && message.type == AIQA_UI_MESSAGE_PAIRING_SHOW) {
            const uint32_t canceled = atomic_load_explicit(
                &s_pairing_ui_canceled_sequence, memory_order_acquire);
            if (canceled != 0U &&
                (int32_t)(message.command_sequence - canceled) <= 0) {
                atomic_store_explicit(
                    &s_pairing_ui_completed_ok, false, memory_order_release);
                atomic_store_explicit(
                    &s_pairing_ui_completed_sequence,
                    message.command_sequence,
                    memory_order_release);
                (void)xSemaphoreGive(s_pairing_ui_ack);
                secure_zero_bytes(&message, sizeof(message));
                continue;
            }
            bool shown = false;
            if (display_ready) {
                const board_wave_175c_display_page_t pairing_page = {
                    .title = "AIQA",
                    .status = "PAIRING",
                    .detail = message.pairing_code,
                    .hint = "ENTER IN CLIENT",
                    .accent_rgb565 = 0x4DFF,
                    .is_error = false,
                    .expression = BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS,
                };
                shown = board_wave_175c_display_show_page(&pairing_page) == ESP_OK;
            }
            pairing_overlay = shown;
            if (!shown) {
                page_dirty = have_last_message;
            }
            atomic_store_explicit(
                &s_pairing_ui_completed_ok, shown, memory_order_release);
            atomic_store_explicit(
                &s_pairing_ui_completed_sequence,
                message.command_sequence,
                memory_order_release);
            (void)xSemaphoreGive(s_pairing_ui_ack);
            secure_zero_bytes(&message, sizeof(message));
            continue;
        }
        if (received && message.type == AIQA_UI_MESSAGE_PAIRING_CLEAR) {
            bool cleared = false;
            if (display_ready) {
                const board_wave_175c_display_page_t page = have_last_message
                    ? aiqa_runtime_ui_page_for_dialogue(
                          last_message.state,
                          last_message.error,
                          &last_message.dialogue)
                    : aiqa_runtime_ui_page_for(AIQA_STATE_BOOT, AIQA_ERROR_NONE);
                cleared = board_wave_175c_display_show_page(&page) == ESP_OK;
            }
            pairing_overlay = false;
            page_dirty = have_last_message && !cleared;
            atomic_store_explicit(
                &s_pairing_ui_completed_ok, cleared, memory_order_release);
            atomic_store_explicit(
                &s_pairing_ui_completed_sequence,
                message.command_sequence,
                memory_order_release);
            (void)xSemaphoreGive(s_pairing_ui_ack);
            secure_zero_bytes(&message, sizeof(message));
            continue;
        }
        if (received && message.type == AIQA_UI_MESSAGE_STATE) {
            last_message = message;
            have_last_message = true;
            page_dirty = true;
            animation_warning_reported = false;
            if (message.error == AIQA_ERROR_NONE) {
                ESP_LOGI("aiqa_ui", "UI state: %s", aiqa_state_name(message.state));
            } else {
                ESP_LOGW("aiqa_ui", "UI state: %s error=%s",
                         aiqa_state_name(message.state),
                         aiqa_error_name(message.error));
            }
            if (pairing_overlay) {
                continue;
            }
        } else if (display_ready && have_last_message && !pairing_overlay) {
            message = last_message;
        } else {
            continue;
        }
        if (display_ready) {
            const bool render_full_page = received || page_dirty;
            const board_wave_175c_display_page_t page =
                aiqa_runtime_ui_page_for_dialogue(message.state, message.error, &message.dialogue);
            display_ret = render_full_page
                              ? board_wave_175c_display_show_page(&page)
                              : board_wave_175c_display_animate_pet(page.expression, page.accent_rgb565);
            if (render_full_page) {
                page_dirty = display_ret != ESP_OK;
            }
            ESP_LOGI("aiqa_ui",
                     "AIQA_DIAG ui_render desired_state=%s error=%s page_status=%s render=%s result=%s dirty=%d",
                     aiqa_state_name(message.state),
                     aiqa_error_name(message.error),
                     page.status != NULL ? page.status : "none",
                     render_full_page ? "full" : "animation",
                     esp_err_to_name(display_ret),
                     page_dirty ? 1 : 0);
            if (display_ret != ESP_OK) {
                if (render_full_page || !animation_warning_reported) {
                    ESP_LOGW("aiqa_ui", "LCD %s draw failed: %s",
                             render_full_page ? "page" : "pet animation",
                             esp_err_to_name(display_ret));
                    animation_warning_reported = !render_full_page;
                }
            } else if (!render_full_page) {
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
            (void)post_critical_app_event(
                aiqa_runtime_asr_failure_event(command.generation));
            continue;
        }
        ret = aiqa_audio_capture_hw_init(&config);
        if (ret == ESP_OK) {
            ret = aiqa_audio_capture_hw_start();
        }
        if (ret != ESP_OK) {
            ESP_LOGE("aiqa_audio", "ES7210 recording start failed: %s", esp_err_to_name(ret));
            aiqa_runtime_recording_reset(&s_recording);
            (void)post_critical_app_event(
                aiqa_runtime_asr_failure_event(command.generation));
            continue;
        }
        bool recording = true;
        bool buffer_overflow = false;
        bool interrupted = false;
        size_t total_bytes = 0, total_samples = 0;
        int16_t session_peak = 0;
        const uint32_t started_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ESP_LOGI("aiqa_audio", "PTT recording session started: max=%u ms", (unsigned)command.max_record_ms);
        while (recording) {
            if (!interaction_generation_is_current(command.generation)) {
                interrupted = true;
                break;
            }
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
            if (xQueuePeek(s_audio_queue, &pending, 0) == pdTRUE &&
                pending.type == AIQA_AUDIO_COMMAND_STOP_RECORDING &&
                pending.generation == command.generation) {
                (void)xQueueReceive(s_audio_queue, &pending, 0);
                recording = false;
            }
        }
        const esp_err_t capture_stop_ret = aiqa_audio_capture_hw_stop();
        (void)memset(samples, 0, sizeof(samples));
        const uint32_t stopped_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ESP_LOGI("aiqa_audio", "PTT recording stopped: bytes=%u mono_samples=%u pcm_bytes=%u peak=%d",
                 (unsigned)total_bytes,
                 (unsigned)total_samples,
                 (unsigned)s_recording.length,
                 (int)session_peak);
        ESP_LOGI("aiqa_audio",
                 "AIQA_DIAG recording generation=%u duration_ms=%u pcm_bytes=%u input_bytes=%u mono_samples=%u peak=%d interrupted=%d overflow=%d stop_status=%s",
                 (unsigned)command.generation,
                 (unsigned)(stopped_ms - started_ms),
                 (unsigned)s_recording.length,
                 (unsigned)total_bytes,
                 (unsigned)total_samples,
                 (int)session_peak,
                 interrupted ? 1 : 0,
                 buffer_overflow ? 1 : 0,
                 esp_err_to_name(capture_stop_ret));
        if (interrupted) {
            aiqa_runtime_recording_reset(&s_recording);
            continue;
        }
        if (buffer_overflow || !aiqa_runtime_recording_has_audio(&s_recording)) {
            aiqa_runtime_recording_reset(&s_recording);
            const aiqa_event_t failed = buffer_overflow
                ? (aiqa_event_t){
                      .type = AIQA_EVENT_AUDIO_TOO_LONG,
                      .error = AIQA_ERROR_AUDIO_TOO_LONG,
                      .value = command.generation,
                  }
                : aiqa_runtime_asr_failure_event(command.generation);
            (void)post_critical_app_event(failed);
            continue;
        }
        aiqa_asr_command_t asr_command = {
            .type = AIQA_ASR_COMMAND_PCM_WAV,
            .generation = command.generation,
            .request_epoch = aiqa_asr_request_epoch_capture(),
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
            (void)post_critical_app_event(
                aiqa_runtime_asr_failure_event(command.generation));
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

    aiqa_boot_gesture_t gesture;
    aiqa_boot_gesture_init(&gesture);
    ESP_LOGI("aiqa_button", "BOOT gesture arbiter ready: PTT=%u ms max=%u ms pairing=3 taps reset=5 taps",
             (unsigned)policy.long_press_ms,
             (unsigned)policy.max_record_ms);

    while (true) {
        bool pressed = false;
        esp_err_t ret = board_wave_175c_boot_button_is_pressed(&pressed);
        if (ret == ESP_OK) {
            uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            const aiqa_boot_gesture_output_t output =
                aiqa_boot_gesture_update(&gesture, &policy, pressed, now_ms);
            aiqa_event_t event = {0};
            if (aiqa_runtime_ptt_output_to_event(output.ptt, &event)) {
                ESP_LOGI("aiqa_button", "PTT output: %s", aiqa_event_name(event.type));
                esp_err_t post_ret = post_critical_app_event(event);
                if (post_ret != ESP_OK) {
                    ESP_LOGW("aiqa_button", "Failed to post PTT event: %s", esp_err_to_name(post_ret));
                }
            }
            if (output.local_action != AIQA_BOOT_LOCAL_NONE) {
                const aiqa_pairing_local_action_t action =
                    output.local_action == AIQA_BOOT_LOCAL_PAIRING
                        ? AIQA_PAIRING_LOCAL_START
                        : AIQA_PAIRING_LOCAL_RESET;
                if (!aiqa_pairing_esp_post_local_action(action)) {
                    ESP_LOGW("aiqa_button", "Failed to post local management action");
                }
            }
        } else {
            ESP_LOGW("aiqa_button", "BOOT read failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(policy.poll_interval_ms));
    }
}

static aiqa_management_result_t map_transaction_result(
    aiqa_config_transaction_status_t status)
{
    switch (status) {
    case AIQA_CONFIG_TRANSACTION_OK:
        return AIQA_MANAGEMENT_OK;
    case AIQA_CONFIG_TRANSACTION_ERR_REVISION_CONFLICT:
        return AIQA_MANAGEMENT_REVISION_CONFLICT;
    case AIQA_CONFIG_TRANSACTION_ERR_REVISION_EXHAUSTED:
        return AIQA_MANAGEMENT_REVISION_EXHAUSTED;
    case AIQA_CONFIG_TRANSACTION_ERR_BUSY:
        return AIQA_MANAGEMENT_BUSY;
    case AIQA_CONFIG_TRANSACTION_ERR_SSID:
    case AIQA_CONFIG_TRANSACTION_ERR_PASSWORD:
    case AIQA_CONFIG_TRANSACTION_ERR_PASSWORD_ACTION:
    case AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT:
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    case AIQA_CONFIG_TRANSACTION_ERR_TRIAL_FAILED_ROLLED_BACK:
        return AIQA_MANAGEMENT_WIFI_UNREACHABLE_ROLLED_BACK;
    case AIQA_CONFIG_TRANSACTION_ERR_STAGE_FAILED:
    case AIQA_CONFIG_TRANSACTION_ERR_VERIFY_FAILED:
    case AIQA_CONFIG_TRANSACTION_ERR_ACTIVATE_FAILED_ROLLED_BACK:
        return AIQA_MANAGEMENT_PERSISTENCE_FAILED_ROLLED_BACK;
    case AIQA_CONFIG_TRANSACTION_ERR_NETWORK_RECOVERY_FAILED:
    case AIQA_CONFIG_TRANSACTION_ERR_ACTIVATION_INDETERMINATE:
    case AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED:
    case AIQA_CONFIG_TRANSACTION_ERR_RETIRED_SLOT_CLEANUP_FAILED:
    case AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED:
        return AIQA_MANAGEMENT_RECOVERY_REQUIRED;
    default:
        return AIQA_MANAGEMENT_INTERNAL_ERROR;
    }
}

static aiqa_management_result_t apply_management_wifi_job(
    const aiqa_management_wifi_job_t *job)
{
    if (job == NULL) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    aiqa_config_record_t active = {0};
    bool found = false;
    esp_err_t load_ret = aiqa_config_nvs_load_active_record(&active, &found);
    if (load_ret != ESP_OK || !found) {
        aiqa_config_record_secure_clear(&active);
        return load_ret == ESP_OK ? AIQA_MANAGEMENT_NOT_READY
                                  : AIQA_MANAGEMENT_INTERNAL_ERROR;
    }

    aiqa_net_transaction_adapter_t network_adapter = {0};
    const aiqa_net_policy_t policy = aiqa_net_default_policy();
    aiqa_net_transaction_adapter_init(&network_adapter, &policy);
    aiqa_config_transaction_ports_t ports = {
        .storage = aiqa_config_nvs_storage_ports(),
        .network = aiqa_net_transaction_ports(&network_adapter),
    };
    aiqa_config_transaction_t transaction = {0};
    if (!aiqa_config_transaction_init(&transaction, &active, &ports)) {
        aiqa_config_record_secure_clear(&active);
        aiqa_config_transaction_secure_clear(&transaction);
        return AIQA_MANAGEMENT_INTERNAL_ERROR;
    }

    const aiqa_wifi_update_t request = {
        .base_revision = job->update.base_revision,
        .password_action = job->update.password_action,
        .ssid = job->update.ssid,
        .password = job->update.password,
    };
    const aiqa_config_transaction_status_t transaction_status =
        aiqa_config_transaction_apply_wifi(&transaction, &request, NULL);
    aiqa_management_result_t result = map_transaction_result(transaction_status);
    if (result == AIQA_MANAGEMENT_OK) {
        aiqa_config_record_t updated = {0};
        if (aiqa_config_transaction_copy_active(&transaction, &updated) !=
                AIQA_CONFIG_TRANSACTION_READ_OK ||
            !store_config_record(&updated)) {
            result = AIQA_MANAGEMENT_INTERNAL_ERROR;
        }
        aiqa_config_record_secure_clear(&updated);
    } else if (result == AIQA_MANAGEMENT_RECOVERY_REQUIRED) {
        clear_config_snapshot();
        (void)aiqa_runtime_post_event((aiqa_event_t){
            .type = AIQA_EVENT_CONFIG_CORRUPT,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
        });
    }

    aiqa_config_transaction_secure_clear(&transaction);
    aiqa_config_record_secure_clear(&active);
    return result;
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

        if (command.type == AIQA_NET_COMMAND_DISCONNECT) {
            esp_err_t ret = aiqa_net_disconnect_wifi();
            if (ret != ESP_OK) {
                ESP_LOGE("aiqa_net", "Wi-Fi disconnect failed: %s", esp_err_to_name(ret));
            }
            continue;
        }
        if (command.type == AIQA_NET_COMMAND_FACTORY_RESET) {
            const esp_err_t forget_ret = aiqa_net_forget_wifi();
            if (forget_ret != ESP_OK) {
                ESP_LOGW("aiqa_net", "Wi-Fi RAM credential cleanup failed: %s",
                         esp_err_to_name(forget_ret));
            }
            clear_config_snapshot();
            const esp_err_t erase_ret = aiqa_config_erase_nvs_namespace();
            const esp_err_t reset_ret = erase_ret != ESP_OK ? erase_ret : forget_ret;
            if (reset_ret == ESP_OK) {
                ESP_LOGW("aiqa_net", "Factory reset erased all configuration namespaces");
            } else {
                ESP_LOGE("aiqa_net", "Factory reset failed: %s", esp_err_to_name(reset_ret));
            }
            const esp_err_t post_ret = aiqa_runtime_post_event((aiqa_event_t){
                .type = reset_ret == ESP_OK ? AIQA_EVENT_CONFIG_MISSING
                                            : AIQA_EVENT_CONFIG_CORRUPT,
                .error = reset_ret == ESP_OK ? AIQA_ERROR_CONFIG_MISSING
                                             : AIQA_ERROR_CONFIG_CORRUPT,
                .value = (uint32_t)reset_ret,
            });
            if (post_ret != ESP_OK) {
                ESP_LOGE("aiqa_net", "Failed to publish factory reset result: %s",
                         esp_err_to_name(post_ret));
            }
            continue;
        }
        if (command.type == AIQA_NET_COMMAND_APPLY_WIFI) {
            aiqa_management_result_t result =
                apply_management_wifi_job(command.wifi_update_job);
            const uint32_t operation_id = command.wifi_update_job != NULL
                                              ? command.wifi_update_job->operation_id
                                              : 0;
            if (!complete_management_wifi_operation(operation_id, result)) {
                ESP_LOGE("aiqa_net", "Failed to complete Wi-Fi management operation");
            }
            release_management_wifi_job(command.wifi_update_job);
            continue;
        }
        if (command.type != AIQA_NET_COMMAND_CONNECT) {
            continue;
        }
        if (command.connect_job == NULL) {
            ESP_LOGE("aiqa_net", "Wi-Fi connect command missing owned job");
            continue;
        }

        aiqa_net_policy_t policy = aiqa_net_default_policy();
        esp_err_t ret = aiqa_net_connect_wifi_and_sync_time(
            &command.connect_job->credentials, &policy);
        release_wifi_connect_job(command.connect_job);
        if (!interaction_generation_is_current(command.generation)) {
            ESP_LOGI("aiqa_net", "Dropping stale network result generation=%u",
                     (unsigned)command.generation);
            (void)aiqa_net_disconnect_wifi();
            continue;
        }
        aiqa_event_t event = {
            .type = ret == ESP_OK ? AIQA_EVENT_NETWORK_READY : AIQA_EVENT_NETWORK_FAILED,
            .error = ret == ESP_OK ? AIQA_ERROR_NONE : AIQA_ERROR_NETWORK_FAILED,
            .value = (uint32_t)ret,
        };
        esp_err_t post_ret = post_critical_app_event(event);
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
            if (interaction_generation_is_current(command.generation)) {
                (void)post_critical_app_event(
                    aiqa_runtime_asr_failure_event(command.generation));
                (void)post_clear_device_intent();
            }
            clear_asr_command_pcm(&command);
            continue;
        }
        if ((command.type == AIQA_ASR_COMMAND_STATIC_SAMPLE && command.audio_ref == NULL) ||
            (command.type == AIQA_ASR_COMMAND_PCM_WAV &&
             (command.pcm == NULL || command.pcm_bytes == 0))) {
            if (interaction_generation_is_current(command.generation)) {
                (void)post_critical_app_event(
                    aiqa_runtime_asr_failure_event(command.generation));
                (void)post_clear_device_intent();
            }
            clear_asr_command_pcm(&command);
            continue;
        }
        if (!interaction_generation_is_current(command.generation)) {
            ESP_LOGI("aiqa_asr", "Skipping stale ASR command generation=%u",
                     (unsigned)command.generation);
            clear_asr_command_pcm(&command);
            continue;
        }
        aiqa_config_snapshot_t config_snapshot = {0};
        if (!copy_config_snapshot(&config_snapshot)) {
            ESP_LOGE("aiqa_asr", "ASR requested before config snapshot was loaded");
            clear_asr_command_pcm(&command);
            (void)post_critical_app_event((aiqa_event_t){
                .type = AIQA_EVENT_CONFIG_CORRUPT,
                .error = AIQA_ERROR_CONFIG_CORRUPT,
                .value = command.generation,
            });
            (void)post_clear_device_intent();
            continue;
        }

        const aiqa_hardening_policy_t hardening = aiqa_hardening_default_policy();
        const size_t free_internal_heap =
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t largest_internal_block =
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!aiqa_hardening_heap_allows_model_request(&hardening, free_internal_heap) ||
            largest_internal_block < AIQA_ASR_MIN_INTERNAL_LARGEST_BLOCK_BYTES) {
            ESP_LOGE("aiqa_asr",
                     "ASR skipped: internal heap too low (free=%u largest=%u)",
                     (unsigned)free_internal_heap,
                     (unsigned)largest_internal_block);
            aiqa_config_snapshot_secure_clear(&config_snapshot);
            clear_asr_command_pcm(&command);
            (void)post_critical_app_event(
                aiqa_runtime_asr_failure_event(command.generation));
            (void)post_clear_device_intent();
            continue;
        }

        (void)aiqa_runtime_post_event((aiqa_event_t){
            .type = AIQA_EVENT_ASR_STARTED,
            .error = AIQA_ERROR_NONE,
            .value = command.generation,
        });

        aiqa_asr_result_t result = {0};
        esp_err_t asr_ret = ESP_OK;
        const int64_t asr_started_us = esp_timer_get_time();
        if (command.type == AIQA_ASR_COMMAND_PCM_WAV) {
            const aiqa_asr_pcm_audio_t audio = {
                .pcm = command.pcm,
                .pcm_bytes = command.pcm_bytes,
                .sample_rate_hz = command.sample_rate_hz,
                .bits_per_sample = command.bits_per_sample,
                .channels = command.channels,
            };
            asr_ret = aiqa_asr_transcribe_pcm_wav_once_with_epoch(
                &config_snapshot.config,
                &config_snapshot.secrets,
                &audio,
                command.request_epoch,
                &result);
        } else {
            asr_ret = aiqa_asr_transcribe_once_with_epoch(
                &config_snapshot.config,
                &config_snapshot.secrets,
                command.audio_ref,
                command.request_epoch,
                &result);
        }
        ESP_LOGI("aiqa_asr",
                 "AIQA_DIAG asr_result generation=%u status=%s http=%d transport=%s transcript_bytes=%u elapsed_ms=%u",
                 (unsigned)command.generation,
                 aiqa_asr_status_name(result.status),
                 result.http_status,
                 esp_err_to_name(asr_ret),
                 (unsigned)strlen(result.text),
                 (unsigned)((esp_timer_get_time() - asr_started_us) / 1000));
        aiqa_config_snapshot_secure_clear(&config_snapshot);
        clear_asr_command_pcm(&command);
        if (!lock_current_interaction(command.generation)) {
            ESP_LOGI("aiqa_asr", "Dropping stale ASR result generation=%u",
                     (unsigned)command.generation);
            (void)memset(result.text, 0, sizeof(result.text));
            continue;
        }
        if (result.status == AIQA_ASR_OK) {
            (void)snprintf(s_latest_transcript, sizeof(s_latest_transcript), "%s", result.text);
            aiqa_dialogue_view_set_user(&s_latest_dialogue, result.text);
            (void)handle_local_command_transcript(result.text);
            ESP_LOGI("aiqa_asr", "ASR transcript ready: %u bytes", (unsigned)strlen(result.text));
        }
        unlock_current_interaction();
        if (result.status != AIQA_ASR_OK) {
            (void)post_clear_device_intent();
        }

        aiqa_event_t event = aiqa_runtime_asr_result_to_event(&result, asr_ret);
        event.value = command.generation;
        (void)memset(result.text, 0, sizeof(result.text));
        esp_err_t post_ret = post_critical_app_event(event);
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
    if (!lock_current_interaction(context->generation)) {
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
        unlock_current_interaction();
        return;
    }

    s_latest_dialogue = updated;
    const int64_t now_us = esp_timer_get_time();
    const int64_t min_interval_us =
        (int64_t)AIQA_CHAT_UI_UPDATE_INTERVAL_MS * 1000;
    if (context->last_ui_update_us > 0 &&
        now_us >= context->last_ui_update_us &&
        now_us - context->last_ui_update_us < min_interval_us) {
        unlock_current_interaction();
        return;
    }
    context->last_ui_update_us = now_us;
    unlock_current_interaction();
    (void)aiqa_runtime_post_event((aiqa_event_t){
        .type = AIQA_EVENT_CHAT_TOKEN,
        .error = AIQA_ERROR_NONE,
        .value = context->generation,
    });
}

static esp_err_t tts_playback_status_load(const aiqa_tts_playback_context_t *context)
{
    if (context == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return (esp_err_t)atomic_load_explicit(&context->status, memory_order_acquire);
}

static void tts_playback_fail(aiqa_tts_playback_context_t *context, esp_err_t status)
{
    if (context == NULL || status == ESP_OK) {
        return;
    }
    int expected = ESP_OK;
    (void)atomic_compare_exchange_strong_explicit(
        &context->status,
        &expected,
        status,
        memory_order_acq_rel,
        memory_order_acquire);
}

static bool tts_playback_producer_done(const aiqa_tts_playback_context_t *context)
{
    return context != NULL &&
           atomic_load_explicit(&context->producer_done, memory_order_acquire);
}

static void tts_playback_add_queued_pcm(
    aiqa_tts_playback_context_t *context,
    size_t pcm_bytes)
{
    if (context == NULL || pcm_bytes == 0) {
        return;
    }
    (void)atomic_fetch_add_explicit(
        &context->queued_pcm_bytes, pcm_bytes, memory_order_release);
}

static bool tts_playback_remove_queued_pcm(
    aiqa_tts_playback_context_t *context,
    size_t pcm_bytes)
{
    if (context == NULL || pcm_bytes == 0) {
        return false;
    }

    size_t current = atomic_load_explicit(
        &context->queued_pcm_bytes, memory_order_acquire);
    while (current >= pcm_bytes) {
        if (atomic_compare_exchange_weak_explicit(
                &context->queued_pcm_bytes,
                &current,
                current - pcm_bytes,
                memory_order_acq_rel,
                memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

static aiqa_tts_playback_slot_t *allocate_tts_playback_slots(void)
{
    aiqa_tts_playback_slot_t *slots =
        (aiqa_tts_playback_slot_t *)heap_caps_calloc(AIQA_TTS_PLAYBACK_SLOT_COUNT,
                                                     sizeof(aiqa_tts_playback_slot_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (slots != NULL) {
        return slots;
    }
    slots = (aiqa_tts_playback_slot_t *)heap_caps_calloc(AIQA_TTS_PLAYBACK_SLOT_COUNT,
                                                         sizeof(aiqa_tts_playback_slot_t),
                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (slots != NULL) {
        return slots;
    }
    return (aiqa_tts_playback_slot_t *)heap_caps_calloc(AIQA_TTS_PLAYBACK_SLOT_COUNT,
                                                        sizeof(aiqa_tts_playback_slot_t),
                                                        MALLOC_CAP_8BIT);
}

static void release_tts_playback_slot(aiqa_tts_playback_context_t *context,
                                      aiqa_tts_playback_slot_t *slot)
{
    if (context == NULL || context->free_queue == NULL || slot == NULL) {
        return;
    }
    secure_zero_bytes(slot->pcm, sizeof(slot->pcm));
    slot->pcm_bytes = 0;
    if (xQueueSend(context->free_queue, &slot, 0) != pdTRUE) {
        ESP_LOGW("aiqa_tts", "TTS playback free slot queue full");
    }
}

static void maybe_wait_for_tts_buffer(
    const aiqa_tts_playback_context_t *context,
    const aiqa_tts_playback_policy_t *policy,
    size_t held_pcm_bytes,
    bool recovering_from_starvation)
{
    if (context == NULL || context->play_queue == NULL || policy == NULL ||
        held_pcm_bytes == 0) {
        return;
    }

    const TickType_t start_tick = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(AIQA_TTS_PLAYBACK_PREBUFFER_TIMEOUT_MS);
    while (tts_playback_status_load(context) == ESP_OK &&
           interaction_generation_is_current(context->generation)) {
        size_t buffered_bytes = 0;
        const size_t queued_pcm_bytes = atomic_load_explicit(
            &context->queued_pcm_bytes, memory_order_acquire);
        if (!aiqa_tts_playback_buffered_bytes(
                held_pcm_bytes, queued_pcm_bytes, &buffered_bytes)) {
            return;
        }
        if (aiqa_tts_playback_buffer_ready(
                policy,
                buffered_bytes,
                tts_playback_producer_done(context),
                recovering_from_starvation)) {
            return;
        }
        if ((xTaskGetTickCount() - start_tick) >= timeout_ticks) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void tts_stream_playback_task(void *arg)
{
    aiqa_tts_playback_context_t *context = (aiqa_tts_playback_context_t *)arg;
    if (context == NULL) {
        vTaskDelete(NULL);
        return;
    }

    bool started = false;
    bool low_water_active = false;
    esp_err_t playback_ret = ESP_OK;
    aiqa_audio_playback_config_t playback_config = aiqa_audio_playback_default_config();
    playback_config.volume_percent = current_volume_percent();
    aiqa_tts_playback_policy_t buffer_policy = {0};
    if (!aiqa_tts_playback_policy_init(
            &buffer_policy,
            playback_config.sample_rate_hz,
            playback_config.bits_per_sample,
            playback_config.channels,
            AIQA_TTS_PLAYBACK_INITIAL_BUFFER_MS,
            AIQA_TTS_PLAYBACK_RESUME_BUFFER_MS)) {
        tts_playback_fail(context, ESP_ERR_INVALID_STATE);
        xSemaphoreGive(context->done);
        vTaskDelete(NULL);
        return;
    }

    while (tts_playback_status_load(context) == ESP_OK) {
        aiqa_tts_playback_slot_t *slot = NULL;
        const UBaseType_t queued_slots = uxQueueMessagesWaiting(context->play_queue);
        const size_t queued_pcm_bytes = atomic_load_explicit(
            &context->queued_pcm_bytes, memory_order_acquire);
        const bool producer_done = tts_playback_producer_done(context);
        const bool queue_was_empty = started && queued_slots == 0 && !producer_done;
        if (started && !producer_done) {
            if (queued_pcm_bytes < context->min_queued_pcm_bytes) {
                context->min_queued_pcm_bytes = queued_pcm_bytes;
            }
            const bool is_low_water =
                queued_pcm_bytes <= AIQA_TTS_PLAYBACK_LOW_WATER_BYTES;
            if (is_low_water && !low_water_active) {
                context->low_water_hits += 1u;
            }
            low_water_active = is_low_water;
        } else {
            low_water_active = false;
        }

        const int64_t starvation_started_us = queue_was_empty ? esp_timer_get_time() : 0;
        bool received = false;
        while (!received) {
            if (xQueueReceive(context->play_queue,
                              &slot,
                              pdMS_TO_TICKS(AIQA_TTS_PLAYBACK_QUEUE_POLL_MS)) == pdTRUE) {
                received = true;
                break;
            }
            const esp_err_t status = tts_playback_status_load(context);
            if (status != ESP_OK || !interaction_generation_is_current(context->generation)) {
                playback_ret = status != ESP_OK ? status : ESP_ERR_INVALID_STATE;
                break;
            }
        }
        if (!received) {
            break;
        }
        if (slot == NULL) {
            break;
        }
        if (!tts_playback_remove_queued_pcm(context, slot->pcm_bytes)) {
            playback_ret = ESP_ERR_INVALID_STATE;
            release_tts_playback_slot(context, slot);
            break;
        }
        if (!interaction_generation_is_current(context->generation)) {
            playback_ret = ESP_ERR_INVALID_STATE;
            release_tts_playback_slot(context, slot);
            break;
        }

        if (!started) {
            maybe_wait_for_tts_buffer(
                context, &buffer_policy, slot->pcm_bytes, false);
            const esp_err_t status = tts_playback_status_load(context);
            if (status != ESP_OK ||
                !interaction_generation_is_current(context->generation)) {
                playback_ret = status != ESP_OK ? status : ESP_ERR_INVALID_STATE;
                release_tts_playback_slot(context, slot);
                break;
            }
            playback_ret = aiqa_audio_playback_hw_init(&playback_config);
            if (playback_ret == ESP_OK) {
                playback_ret = aiqa_audio_playback_hw_start();
            }
            if (playback_ret != ESP_OK) {
                (void)aiqa_audio_playback_hw_stop();
                release_tts_playback_slot(context, slot);
                break;
            }
            started = true;
        } else if (queue_was_empty) {
            const uint64_t queue_wait_us =
                (uint64_t)(esp_timer_get_time() - starvation_started_us);
            if (aiqa_tts_playback_wait_is_starvation(
                    queue_wait_us, AIQA_TTS_PLAYBACK_STARVATION_THRESHOLD_MS)) {
                const int64_t rebuffer_started_us = esp_timer_get_time();
                maybe_wait_for_tts_buffer(
                    context, &buffer_policy, slot->pcm_bytes, true);
                const uint64_t starvation_us =
                    (uint64_t)(esp_timer_get_time() - starvation_started_us);
                const uint64_t rebuffer_wait_us =
                    (uint64_t)(esp_timer_get_time() - rebuffer_started_us);
                aiqa_tts_playback_record_starvation(&context->starvation, starvation_us);
                ESP_LOGW("aiqa_tts",
                         "TTS playback rebuffered after queue starvation: queue_wait_ms=%u rebuffer_wait_ms=%u total_ms=%u queued_bytes=%u",
                         (unsigned)(queue_wait_us / 1000u),
                         (unsigned)(rebuffer_wait_us / 1000u),
                         (unsigned)(starvation_us / 1000u),
                         (unsigned)atomic_load_explicit(
                             &context->queued_pcm_bytes, memory_order_acquire));
                const esp_err_t status = tts_playback_status_load(context);
                if (status != ESP_OK ||
                    !interaction_generation_is_current(context->generation)) {
                    playback_ret = status != ESP_OK ? status : ESP_ERR_INVALID_STATE;
                    release_tts_playback_slot(context, slot);
                    break;
                }
            }
        }

        size_t offset = 0;
        while (offset < slot->pcm_bytes &&
               tts_playback_status_load(context) == ESP_OK &&
               interaction_generation_is_current(context->generation)) {
            size_t write_bytes = slot->pcm_bytes - offset;
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
            playback_ret = aiqa_audio_playback_hw_write_pcm(slot->pcm + offset, write_bytes);
            if (playback_ret != ESP_OK) {
                break;
            }
            offset += write_bytes;
            context->played_bytes += write_bytes;
        }
        release_tts_playback_slot(context, slot);
        if (playback_ret != ESP_OK || !interaction_generation_is_current(context->generation)) {
            if (playback_ret == ESP_OK) {
                playback_ret = ESP_ERR_INVALID_STATE;
            }
            break;
        }
    }

    tts_playback_fail(context, playback_ret);

    aiqa_tts_playback_slot_t *pending = NULL;
    while (xQueueReceive(context->play_queue, &pending, 0) == pdTRUE) {
        if (pending != NULL) {
            (void)tts_playback_remove_queued_pcm(context, pending->pcm_bytes);
        }
        release_tts_playback_slot(context, pending);
    }

    if (started) {
        esp_err_t stop_ret = aiqa_audio_playback_hw_stop();
        if (playback_ret == ESP_OK) {
            playback_ret = stop_ret;
        }
        tts_playback_fail(context, stop_ret);
    }
    xSemaphoreGive(context->done);
    vTaskDelete(NULL);
}

static bool tts_playback_audio_callback(const uint8_t *pcm, size_t pcm_bytes, void *user_ctx)
{
    aiqa_tts_playback_context_t *context = (aiqa_tts_playback_context_t *)user_ctx;
    if (context == NULL || pcm == NULL || pcm_bytes == 0 ||
        tts_playback_status_load(context) != ESP_OK) {
        return false;
    }
    if (!interaction_generation_is_current(context->generation)) {
        tts_playback_fail(context, ESP_ERR_INVALID_STATE);
        return false;
    }
    if ((pcm_bytes % sizeof(int16_t)) != 0) {
        tts_playback_fail(context, ESP_ERR_INVALID_SIZE);
        return false;
    }

    size_t offset = 0;
    while (offset < pcm_bytes) {
        if (tts_playback_status_load(context) != ESP_OK) {
            return false;
        }
        if (!interaction_generation_is_current(context->generation)) {
            tts_playback_fail(context, ESP_ERR_INVALID_STATE);
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
            tts_playback_fail(context, ESP_ERR_INVALID_SIZE);
            return false;
        }

        aiqa_tts_playback_slot_t *slot = NULL;
        if (xQueueReceive(context->free_queue, &slot, pdMS_TO_TICKS(1000)) != pdTRUE ||
            slot == NULL) {
            tts_playback_fail(context, ESP_ERR_TIMEOUT);
            return false;
        }
        if (tts_playback_status_load(context) != ESP_OK ||
            !interaction_generation_is_current(context->generation)) {
            release_tts_playback_slot(context, slot);
            if (!interaction_generation_is_current(context->generation)) {
                tts_playback_fail(context, ESP_ERR_INVALID_STATE);
            }
            return false;
        }

        (void)memcpy(slot->pcm, pcm + offset, chunk_bytes);
        slot->pcm_bytes = chunk_bytes;
        tts_playback_add_queued_pcm(context, chunk_bytes);
        if (xQueueSend(context->play_queue, &slot, pdMS_TO_TICKS(1000)) != pdTRUE) {
            (void)tts_playback_remove_queued_pcm(context, chunk_bytes);
            release_tts_playback_slot(context, slot);
            tts_playback_fail(context, ESP_ERR_TIMEOUT);
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
    if (context == NULL || context->play_queue == NULL || context->done == NULL) {
        return;
    }

    atomic_store_explicit(&context->producer_done, true, memory_order_release);
    aiqa_tts_playback_slot_t *final_slot = NULL;
    if (xQueueSend(context->play_queue, &final_slot, pdMS_TO_TICKS(2000)) != pdTRUE) {
        tts_playback_fail(context, ESP_ERR_TIMEOUT);
        (void)aiqa_audio_playback_hw_stop();
    }
    if (xSemaphoreTake(context->done, pdMS_TO_TICKS(AIQA_TTS_PLAYBACK_FINISH_TIMEOUT_MS)) != pdTRUE) {
        tts_playback_fail(context, ESP_ERR_TIMEOUT);
        (void)aiqa_audio_playback_hw_stop();
        (void)xSemaphoreTake(context->done, portMAX_DELAY);
    }
}

static void delete_tts_playback_context_resources(aiqa_tts_playback_context_t *context)
{
    if (context == NULL) {
        return;
    }
    if (context->play_queue != NULL) {
        vQueueDelete(context->play_queue);
        context->play_queue = NULL;
    }
    if (context->free_queue != NULL) {
        vQueueDelete(context->free_queue);
        context->free_queue = NULL;
    }
    if (context->done != NULL) {
        vSemaphoreDelete(context->done);
        context->done = NULL;
    }
    if (context->slots != NULL) {
        secure_zero_bytes(context->slots,
                          AIQA_TTS_PLAYBACK_SLOT_COUNT * sizeof(context->slots[0]));
        heap_caps_free(context->slots);
        context->slots = NULL;
    }
}

static void speak_pet_answer(
    const char *answer,
    uint32_t generation,
    uint32_t request_epoch,
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets)
{
    if (answer == NULL || answer[0] == '\0' || config == NULL || secrets == NULL) {
        return;
    }
    if (!interaction_generation_is_current(generation)) {
        return;
    }

    aiqa_tts_playback_context_t playback_context = {
        .play_queue = xQueueCreate(AIQA_TTS_PLAYBACK_PLAY_QUEUE_DEPTH,
                                   sizeof(aiqa_tts_playback_slot_t *)),
        .free_queue = xQueueCreate(AIQA_TTS_PLAYBACK_SLOT_COUNT,
                                   sizeof(aiqa_tts_playback_slot_t *)),
        .done = xSemaphoreCreateBinary(),
        .slots = allocate_tts_playback_slots(),
        .generation = generation,
        .producer_done = ATOMIC_VAR_INIT(false),
        .status = ATOMIC_VAR_INIT(ESP_OK),
        .queued_pcm_bytes = ATOMIC_VAR_INIT(0),
        .audio_bytes = 0,
        .audio_chunks = 0,
        .played_bytes = 0,
        .min_queued_pcm_bytes =
            AIQA_TTS_PLAYBACK_SLOT_COUNT * AIQA_AUDIO_PLAYBACK_CHUNK_BYTES,
        .low_water_hits = 0,
        .starvation = {0},
    };
    aiqa_tts_playback_starvation_stats_init(&playback_context.starvation);
    if (playback_context.play_queue == NULL || playback_context.free_queue == NULL ||
        playback_context.done == NULL || playback_context.slots == NULL) {
        delete_tts_playback_context_resources(&playback_context);
        ESP_LOGW("aiqa_tts", "TTS streaming playback resources allocation failed");
        return;
    }
    for (size_t index = 0; index < AIQA_TTS_PLAYBACK_SLOT_COUNT; ++index) {
        aiqa_tts_playback_slot_t *slot = &playback_context.slots[index];
        if (xQueueSend(playback_context.free_queue, &slot, 0) != pdTRUE) {
            delete_tts_playback_context_resources(&playback_context);
            ESP_LOGW("aiqa_tts", "TTS streaming playback slot initialization failed");
            return;
        }
    }

    if (xTaskCreate(tts_stream_playback_task,
                    "aiqa_tts_play",
                    AIQA_TASK_STACK_TTS_PLAYBACK,
                    &playback_context,
                    6,
                    NULL) != pdPASS) {
        delete_tts_playback_context_resources(&playback_context);
        ESP_LOGW("aiqa_tts", "TTS streaming playback task creation failed");
        return;
    }

    aiqa_tts_result_t tts_result = {0};
    esp_err_t tts_ret = aiqa_tts_speak_streaming_with_epoch(
        config,
        secrets,
        answer,
        request_epoch,
        tts_playback_audio_callback,
        &playback_context,
        &tts_result);
    finish_tts_stream_playback(&playback_context);
    delete_tts_playback_context_resources(&playback_context);

    if (!interaction_generation_is_current(generation)) {
        return;
    }

    const esp_err_t playback_status = tts_playback_status_load(&playback_context);
    if (tts_ret == ESP_OK && tts_result.status == AIQA_TTS_OK &&
        playback_status == ESP_OK) {
        ESP_LOGI("aiqa_tts", "Pet reply streaming playback complete: chunks=%u bytes=%u played=%u min_queued_bytes=%u low_water=%u starvation=%u starvation_total_ms=%u starvation_max_ms=%u",
                 (unsigned)playback_context.audio_chunks,
                 (unsigned)playback_context.audio_bytes,
                 (unsigned)playback_context.played_bytes,
                 (unsigned)playback_context.min_queued_pcm_bytes,
                 (unsigned)playback_context.low_water_hits,
                 (unsigned)playback_context.starvation.count,
                 (unsigned)(playback_context.starvation.total_wait_us / 1000u),
                 (unsigned)(playback_context.starvation.max_wait_us / 1000u));
        return;
    }

    ESP_LOGW("aiqa_tts", "Pet reply playback skipped/failed: transport=%s tts=%s http=%d audio_bytes=%u playback=%s chunks=%u min_queued_bytes=%u low_water=%u starvation=%u starvation_max_ms=%u",
             esp_err_to_name(tts_ret),
             aiqa_tts_status_name(tts_result.status),
             tts_result.http_status,
             (unsigned)tts_result.audio_bytes,
             esp_err_to_name(playback_status),
             (unsigned)playback_context.audio_chunks,
             (unsigned)playback_context.min_queued_pcm_bytes,
             (unsigned)playback_context.low_water_hits,
             (unsigned)playback_context.starvation.count,
             (unsigned)(playback_context.starvation.max_wait_us / 1000u));
}

static void complete_device_intent_reply(
    const char *reply,
    uint32_t generation,
    uint32_t tts_request_epoch,
    const aiqa_config_snapshot_t *config_snapshot)
{
    if (reply == NULL || reply[0] == '\0' || config_snapshot == NULL ||
        !interaction_generation_is_current(generation)) {
        return;
    }
    aiqa_dialogue_view_set_pet(&s_latest_dialogue, reply);
    speak_pet_answer(
        reply, generation, tts_request_epoch,
        &config_snapshot->config, &config_snapshot->secrets);
    if (interaction_generation_is_current(generation)) {
        (void)aiqa_runtime_post_event((aiqa_event_t){
            .type = AIQA_EVENT_CHAT_DONE,
            .error = AIQA_ERROR_NONE,
            .value = generation,
        });
    }
}

static const char *device_intent_failure_reply(aiqa_chat_status_t status)
{
    switch (status) {
    case AIQA_CHAT_ERR_AUTH:
        return "云端鉴权失败，请检查聊天服务密钥。";
    case AIQA_CHAT_ERR_RATE_LIMITED:
        return "云端请求过于频繁，请稍后重试。";
    case AIQA_CHAT_ERR_TIMEOUT:
        return "云端意图请求超时，请检查网络后重试。";
    case AIQA_CHAT_ERR_PARSE:
        return "云端返回的意图格式异常，请重试。";
    case AIQA_CHAT_ERR_BUFFER_TOO_SMALL:
        return "云端意图响应过长，已拒绝执行。";
    case AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER:
        return "当前聊天模型不支持安全的云端意图理解。";
    case AIQA_CHAT_ERR_INVALID_ARG:
    case AIQA_CHAT_ERR_PROVIDER:
    default:
        return "云端意图请求失败，请检查网络和服务配置。";
    }
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
        if (command.type == AIQA_CHAT_COMMAND_CLEAR_DEVICE_INTENT) {
            if (interaction_generation_is_current(command.generation)) {
                aiqa_device_intent_controller_clear(&s_device_intent_controller);
            }
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
        aiqa_config_snapshot_t config_snapshot = {0};
        if (!copy_config_snapshot(&config_snapshot)) {
            ESP_LOGE("aiqa_chat", "Chat requested before config snapshot was loaded");
            (void)memset(command.prompt, 0, sizeof(command.prompt));
            (void)aiqa_runtime_post_event((aiqa_event_t){
                .type = AIQA_EVENT_CONFIG_CORRUPT,
                .error = AIQA_ERROR_CONFIG_CORRUPT,
                .value = command.generation,
            });
            continue;
        }
        if (command.type == AIQA_CHAT_COMMAND_LOCAL_PET_REPLY) {
            ESP_LOGI("aiqa_chat", "Playing local pet reply");
            speak_pet_answer(
                command.prompt,
                command.generation,
                command.tts_request_epoch,
                &config_snapshot.config,
                &config_snapshot.secrets);
            (void)memset(command.prompt, 0, sizeof(command.prompt));
            aiqa_config_snapshot_secure_clear(&config_snapshot);
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
            aiqa_config_snapshot_secure_clear(&config_snapshot);
            continue;
        }

        (void)aiqa_runtime_post_event((aiqa_event_t){
            .type = AIQA_EVENT_CHAT_STARTED,
            .error = AIQA_ERROR_NONE,
            .value = command.generation,
        });

        const aiqa_device_intent_ports_t intent_ports = device_intent_ports();
        char intent_reply[AIQA_DEVICE_INTENT_REPLY_MAX_LEN] = {0};
        if (aiqa_device_intent_controller_has_pending(&s_device_intent_controller)) {
            if (!lock_current_interaction(command.generation)) {
                secure_zero_bytes(command.prompt, sizeof(command.prompt));
                aiqa_config_snapshot_secure_clear(&config_snapshot);
                continue;
            }
            const bool handled =
                aiqa_device_intent_controller_handle_pending_transcript(
                    &s_device_intent_controller,
                    command.prompt,
                    command.generation,
                    esp_timer_get_time(),
                    &intent_ports,
                    intent_reply,
                    sizeof(intent_reply));
            unlock_current_interaction();
            if (handled) {
                complete_device_intent_reply(
                    intent_reply, command.generation, command.tts_request_epoch,
                    &config_snapshot);
                secure_zero_bytes(intent_reply, sizeof(intent_reply));
                secure_zero_bytes(command.prompt, sizeof(command.prompt));
                aiqa_config_snapshot_secure_clear(&config_snapshot);
                continue;
            }
        }

        const aiqa_provider_caps_t *chat_caps =
            aiqa_provider_caps_for(config_snapshot.config.active_provider);
        if (chat_caps == NULL || !chat_caps->supports_device_intent_route) {
            complete_device_intent_reply(
                "当前聊天模型不支持安全的云端意图理解，请切换到通义千问。",
                command.generation,
                command.tts_request_epoch,
                &config_snapshot);
            secure_zero_bytes(command.prompt, sizeof(command.prompt));
            aiqa_config_snapshot_secure_clear(&config_snapshot);
            continue;
        }
        {
            aiqa_chat_intent_result_t intent_result = {0};
            const esp_err_t intent_ret = aiqa_chat_classify_device_intent_once_with_epoch(
                &config_snapshot.config,
                &config_snapshot.secrets,
                command.prompt,
                command.request_epoch,
                &intent_result);
            if (!interaction_generation_is_current(command.generation)) {
                aiqa_device_intent_clear(&intent_result.intent);
                secure_zero_bytes(command.prompt, sizeof(command.prompt));
                aiqa_config_snapshot_secure_clear(&config_snapshot);
                continue;
            }
            if (intent_ret != ESP_OK || intent_result.status != AIQA_CHAT_OK) {
                ESP_LOGE("aiqa_chat", "Cloud intent classification failed closed: status=%s http=%d",
                         aiqa_chat_status_name(intent_result.status),
                         intent_result.http_status);
                complete_device_intent_reply(
                    device_intent_failure_reply(intent_result.status),
                    command.generation,
                    command.tts_request_epoch,
                    &config_snapshot);
                aiqa_device_intent_clear(&intent_result.intent);
                secure_zero_bytes(command.prompt, sizeof(command.prompt));
                aiqa_config_snapshot_secure_clear(&config_snapshot);
                continue;
            }
            if (intent_result.intent.type != AIQA_DEVICE_INTENT_NONE) {
                if (!lock_current_interaction(command.generation)) {
                    aiqa_device_intent_clear(&intent_result.intent);
                    secure_zero_bytes(command.prompt, sizeof(command.prompt));
                    aiqa_config_snapshot_secure_clear(&config_snapshot);
                    continue;
                }
                const bool handled =
                    aiqa_device_intent_controller_handle_cloud_intent(
                        &s_device_intent_controller,
                        &intent_result.intent,
                        command.prompt,
                        command.generation,
                        esp_timer_get_time(),
                        &intent_ports,
                        intent_reply,
                        sizeof(intent_reply));
                unlock_current_interaction();
                aiqa_device_intent_clear(&intent_result.intent);
                if (!handled) {
                    (void)snprintf(intent_reply, sizeof(intent_reply),
                                   "%s", "云端意图无效，请重新说。");
                }
                complete_device_intent_reply(
                    intent_reply, command.generation, command.tts_request_epoch,
                    &config_snapshot);
                secure_zero_bytes(intent_reply, sizeof(intent_reply));
                secure_zero_bytes(command.prompt, sizeof(command.prompt));
                aiqa_config_snapshot_secure_clear(&config_snapshot);
                continue;
            }
            aiqa_device_intent_clear(&intent_result.intent);
        }

        char prompt_snapshot[AIQA_RUNTIME_CHAT_PROMPT_MAX_LEN] = {0};
        (void)snprintf(prompt_snapshot, sizeof(prompt_snapshot), "%s", command.prompt);
        char conversation_memory[AIQA_CONVERSATION_MEMORY_CONTEXT_MAX_LEN] = {0};
        char profile_context[AIQA_ASSISTANT_PROFILE_CONTEXT_MAX_LEN] = {0};
        (void)aiqa_assistant_profile_build_context(
            &config_snapshot.user_prefs.assistant_profile,
            profile_context,
            sizeof(profile_context));
        const bool has_conversation_context =
            aiqa_conversation_memory_build_context(
                &s_conversation_memory,
                conversation_memory,
                sizeof(conversation_memory));

        aiqa_chat_result_t result = {0};
        aiqa_chat_stream_ui_context_t stream_context = {
            .generation = command.generation,
            .answer = {0},
        };
        const bool use_stream = config_snapshot.config.stream &&
                                chat_caps != NULL &&
                                chat_caps->supports_chat_stream;
        const char *response_language = aiqa_language_chat_code(
            config_snapshot.user_prefs.dialogue_language);
        struct tm current_local_time = {0};
        const bool current_local_time_ready = local_time_ready(&current_local_time);
        esp_err_t chat_ret = use_stream
                                 ? aiqa_chat_send_streaming_with_contexts_epoch(
                                       &config_snapshot.config,
                                       &config_snapshot.secrets,
                                       command.prompt,
                                       response_language,
                                       has_conversation_context ? conversation_memory : NULL,
                                       profile_context[0] != '\0' ? profile_context : NULL,
                                       current_local_time_ready ? &current_local_time : NULL,
                                       command.request_epoch,
                                       chat_stream_delta_callback,
                                       &stream_context,
                                       &result)
                                 : aiqa_chat_send_once_with_contexts_epoch(
                                       &config_snapshot.config,
                                       &config_snapshot.secrets,
                                       command.prompt,
                                       response_language,
                                       has_conversation_context ? conversation_memory : NULL,
                                       profile_context[0] != '\0' ? profile_context : NULL,
                                       current_local_time_ready ? &current_local_time : NULL,
                                       command.request_epoch,
                                       &result);
        (void)memset(command.prompt, 0, sizeof(command.prompt));
        if (!lock_current_interaction(command.generation)) {
            ESP_LOGI("aiqa_chat", "Dropping stale chat result generation=%u",
                     (unsigned)command.generation);
            (void)memset(result.text, 0, sizeof(result.text));
            (void)memset(prompt_snapshot, 0, sizeof(prompt_snapshot));
            (void)memset(conversation_memory, 0, sizeof(conversation_memory));
            (void)memset(profile_context, 0, sizeof(profile_context));
            aiqa_config_snapshot_secure_clear(&config_snapshot);
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
        unlock_current_interaction();

        aiqa_event_t event = aiqa_runtime_chat_result_to_event(&result, chat_ret);
        event.value = command.generation;
        esp_err_t post_ret = post_critical_app_event(event);
        if (post_ret != ESP_OK) {
            ESP_LOGE("aiqa_chat", "Failed to post chat result: %s", esp_err_to_name(post_ret));
        }
        if (result.status == AIQA_CHAT_OK) {
            speak_pet_answer(
                result.text,
                command.generation,
                command.tts_request_epoch,
                &config_snapshot.config,
                &config_snapshot.secrets);
        }
        (void)memset(prompt_snapshot, 0, sizeof(prompt_snapshot));
        (void)memset(conversation_memory, 0, sizeof(conversation_memory));
        (void)memset(profile_context, 0, sizeof(profile_context));
        (void)memset(result.text, 0, sizeof(result.text));
        aiqa_config_snapshot_secure_clear(&config_snapshot);
    }
}

enum {
    AIQA_RUNTIME_START_NOT_STARTED = 0,
    AIQA_RUNTIME_START_NVS,
    AIQA_RUNTIME_START_BOARD_INIT,
    AIQA_RUNTIME_START_BRINGUP_CHECKS,
    AIQA_RUNTIME_START_VALIDATION,
    AIQA_RUNTIME_START_RESOURCES,
    AIQA_RUNTIME_START_READY,
};

static esp_err_t runtime_start_fail(esp_err_t status)
{
    atomic_store_explicit(&s_runtime_start_status, status, memory_order_release);
    atomic_store_explicit(&s_runtime_ready, false, memory_order_release);
    return status;
}

esp_err_t aiqa_runtime_start(void)
{
    atomic_store_explicit(
        &s_runtime_start_phase, AIQA_RUNTIME_START_NVS, memory_order_release);
    atomic_store_explicit(&s_runtime_start_status, ESP_OK, memory_order_release);
    atomic_store_explicit(&s_runtime_ready, false, memory_order_release);
    atomic_store_explicit(&s_interaction_generation, 1, memory_order_release);
    s_pending_local_pet_reply = false;
    aiqa_conversation_memory_init(&s_conversation_memory);
    aiqa_device_intent_controller_init(&s_device_intent_controller);
    (void)memset(s_local_pet_reply, 0, sizeof(s_local_pet_reply));

    esp_err_t start_status = init_nvs();
    if (start_status != ESP_OK) {
        return runtime_start_fail(start_status);
    }
    atomic_store_explicit(
        &s_runtime_start_phase, AIQA_RUNTIME_START_BOARD_INIT, memory_order_release);
    start_status = board_wave_175c_init_minimal();
    if (start_status != ESP_OK) {
        return runtime_start_fail(start_status);
    }
    atomic_store_explicit(
        &s_runtime_start_phase, AIQA_RUNTIME_START_BRINGUP_CHECKS, memory_order_release);
    start_status = board_wave_175c_run_bringup_checks();
    if (start_status != ESP_OK) {
        return runtime_start_fail(start_status);
    }
    atomic_store_explicit(
        &s_runtime_start_phase, AIQA_RUNTIME_START_VALIDATION, memory_order_release);

    const aiqa_provider_caps_t *chat_caps = aiqa_provider_caps_for(AIQA_PROVIDER_DASHSCOPE_CHAT);
    if (chat_caps == NULL || !chat_caps->supports_chat_stream ||
        !chat_caps->supports_device_intent_route) {
        return runtime_start_fail(ESP_ERR_INVALID_STATE);
    }

    aiqa_audio_capture_config_t audio_config = aiqa_audio_capture_default_config();
    if (!aiqa_audio_capture_config_is_safe(&audio_config)) {
        return runtime_start_fail(ESP_ERR_INVALID_STATE);
    }
    ESP_LOGI(TAG, "Audio budget: %u Hz, %u-bit, %u channel, max %u s, max_pcm=%u bytes",
             (unsigned)audio_config.sample_rate_hz,
             (unsigned)audio_config.bits_per_sample,
             (unsigned)audio_config.channels,
             (unsigned)audio_config.max_record_seconds,
             (unsigned)audio_config.max_pcm_bytes);

    atomic_store_explicit(
        &s_runtime_start_phase, AIQA_RUNTIME_START_RESOURCES, memory_order_release);
    s_config_mutex = xSemaphoreCreateMutex();
    s_interaction_commit_mutex = xSemaphoreCreateMutex();
    s_pairing_ui_ack = xSemaphoreCreateBinary();
    s_app_queue = xQueueCreate(AIQA_APP_QUEUE_DEPTH, sizeof(aiqa_event_t));
    s_ui_queue = xQueueCreate(AIQA_UI_QUEUE_DEPTH, sizeof(aiqa_ui_message_t));
    s_ui_state_queue = xQueueCreate(AIQA_UI_STATE_QUEUE_DEPTH, sizeof(aiqa_ui_message_t));
    s_ui_queue_set = xQueueCreateSet(AIQA_UI_QUEUE_DEPTH + AIQA_UI_STATE_QUEUE_DEPTH);
    s_net_queue = xQueueCreate(AIQA_NET_QUEUE_DEPTH, sizeof(aiqa_net_command_t));
    s_chat_queue = xQueueCreate(AIQA_CHAT_QUEUE_DEPTH, sizeof(aiqa_chat_command_t));
    s_audio_queue = xQueueCreate(AIQA_AUDIO_QUEUE_DEPTH, sizeof(aiqa_audio_command_t));
    s_asr_queue = xQueueCreate(AIQA_ASR_QUEUE_DEPTH, sizeof(aiqa_asr_command_t));
    if (s_config_mutex == NULL || s_interaction_commit_mutex == NULL ||
        s_pairing_ui_ack == NULL || s_app_queue == NULL ||
        s_ui_queue == NULL || s_ui_state_queue == NULL || s_ui_queue_set == NULL ||
        s_net_queue == NULL || s_chat_queue == NULL || s_audio_queue == NULL ||
        s_asr_queue == NULL) {
        return runtime_start_fail(ESP_ERR_NO_MEM);
    }
    if (xQueueAddToSet(s_ui_queue, s_ui_queue_set) != pdPASS ||
        xQueueAddToSet(s_ui_state_queue, s_ui_queue_set) != pdPASS) {
        return runtime_start_fail(ESP_ERR_INVALID_STATE);
    }

    s_management_status = (aiqa_management_device_status_t){
        .state = AIQA_STATE_BOOT,
        .error = AIQA_ERROR_NONE,
    };
    const aiqa_management_ports_t management_ports = {
        .context = NULL,
        .authorize = management_authorize,
        .copy_status = management_copy_status,
        .copy_public_config = management_copy_public_config,
        .submit_wifi = management_submit_wifi,
    };
    if (!aiqa_management_service_init(&s_management_service, &management_ports)) {
        return runtime_start_fail(ESP_ERR_INVALID_STATE);
    }
    atomic_store_explicit(&s_management_service_ready, true, memory_order_release);

    if (xTaskCreate(app_state_task, "aiqa_state", AIQA_TASK_STACK_APP, NULL, 8, NULL) != pdPASS ||
        xTaskCreate(ui_task, "aiqa_ui", AIQA_TASK_STACK_UI, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(audio_task, "aiqa_audio", AIQA_TASK_STACK_AUDIO, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(button_task, "aiqa_button", AIQA_TASK_STACK_BUTTON, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(net_task, "aiqa_net", AIQA_TASK_STACK_NET, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(asr_task, "aiqa_asr", AIQA_TASK_STACK_ASR, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(chat_task, "aiqa_chat", AIQA_TASK_STACK_CHAT, NULL, 5, NULL) != pdPASS) {
        return runtime_start_fail(ESP_ERR_NO_MEM);
    }

    atomic_store_explicit(
        &s_runtime_start_phase, AIQA_RUNTIME_START_READY, memory_order_release);
    atomic_store_explicit(&s_runtime_start_status, ESP_OK, memory_order_release);
    atomic_store_explicit(&s_runtime_ready, true, memory_order_release);
    return ESP_OK;
}
