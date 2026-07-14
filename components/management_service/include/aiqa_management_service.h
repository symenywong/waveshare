#pragma once

#include "aiqa_config_nvs.h"
#include "aiqa_state_machine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIQA_MANAGEMENT_OK = 0,
    AIQA_MANAGEMENT_INVALID_REQUEST,
    AIQA_MANAGEMENT_FORBIDDEN,
    AIQA_MANAGEMENT_NOT_READY,
    AIQA_MANAGEMENT_REVISION_CONFLICT,
    AIQA_MANAGEMENT_REVISION_EXHAUSTED,
    AIQA_MANAGEMENT_BUSY,
    AIQA_MANAGEMENT_WIFI_UNREACHABLE_ROLLED_BACK,
    AIQA_MANAGEMENT_PERSISTENCE_FAILED_ROLLED_BACK,
    AIQA_MANAGEMENT_RECOVERY_REQUIRED,
    AIQA_MANAGEMENT_CANCELLED,
    AIQA_MANAGEMENT_INTERNAL_ERROR,
} aiqa_management_result_t;

typedef enum {
    AIQA_MANAGEMENT_OPERATION_NONE = 0,
    AIQA_MANAGEMENT_OPERATION_PENDING,
    AIQA_MANAGEMENT_OPERATION_SUCCEEDED,
    AIQA_MANAGEMENT_OPERATION_FAILED,
} aiqa_management_operation_state_t;

typedef struct {
    uint32_t id;
    aiqa_management_operation_state_t state;
    aiqa_management_result_t result;
} aiqa_management_operation_t;

#define AIQA_MANAGEMENT_UI_STATUS_LEN 17U
#define AIQA_MANAGEMENT_UI_TEXT_LEN 33U
#define AIQA_MANAGEMENT_EXPRESSION_LEN 16U

typedef enum {
    AIQA_MANAGEMENT_CHARGING_UNKNOWN = 0,
    AIQA_MANAGEMENT_CHARGING_DISCHARGING,
    AIQA_MANAGEMENT_CHARGING_ACTIVE,
    AIQA_MANAGEMENT_CHARGING_DONE,
} aiqa_management_charging_state_t;

typedef struct {
    aiqa_state_t state;
    aiqa_error_code_t error;
    uint32_t sequence;
    uint64_t uptime_ms;
    size_t free_heap_bytes;
    struct {
        bool connected;
        bool rssi_available;
        int8_t rssi_dbm;
    } wifi;
    struct {
        bool battery_present;
        bool percent_available;
        uint8_t percent;
        aiqa_management_charging_state_t charging_state;
    } power;
    struct {
        char status[AIQA_MANAGEMENT_UI_STATUS_LEN];
        char detail[AIQA_MANAGEMENT_UI_TEXT_LEN];
        char hint[AIQA_MANAGEMENT_UI_TEXT_LEN];
        char expression[AIQA_MANAGEMENT_EXPRESSION_LEN];
    } ui;
    struct {
        bool available;
        uint32_t revision;
        char chat_provider[AIQA_MAX_PROVIDER_ID_LEN];
        char chat_model[AIQA_MAX_MODEL_LEN];
        bool has_chat_api_key;
        bool has_asr_api_key;
    } config;
    aiqa_management_operation_t latest_operation;
} aiqa_management_device_status_t;

typedef struct {
    uint32_t revision;
    aiqa_public_wifi_config_t wifi;
    char chat_provider[AIQA_MAX_PROVIDER_ID_LEN];
    char chat_model[AIQA_MAX_MODEL_LEN];
    char asr_provider[AIQA_MAX_PROVIDER_ID_LEN];
    char asr_model[AIQA_MAX_MODEL_LEN];
    char tts_provider[AIQA_MAX_PROVIDER_ID_LEN];
    char tts_model[AIQA_MAX_MODEL_LEN];
    char tts_voice[AIQA_MAX_TTS_VOICE_LEN];
    bool stream;
    bool hide_reasoning;
    int max_completion_tokens;
    bool has_chat_api_key;
    bool has_asr_api_key;
    aiqa_user_prefs_t user_prefs;
} aiqa_management_public_config_t;

typedef struct {
    uint32_t base_revision;
    aiqa_wifi_password_action_t password_action;
    char ssid[AIQA_MAX_WIFI_SSID_LEN];
    char password[AIQA_MAX_WIFI_PASSWORD_LEN];
} aiqa_management_owned_wifi_update_t;

typedef struct {
    uint32_t session_id;
} aiqa_management_security_context_t;

typedef enum {
    AIQA_MANAGEMENT_CAPABILITY_READ = 0,
    AIQA_MANAGEMENT_CAPABILITY_MANAGE_CONFIG,
} aiqa_management_capability_t;

typedef struct {
    void *context;
    bool (*authorize)(
        void *context,
        uint32_t session_id,
        aiqa_management_capability_t capability);
    bool (*copy_status)(void *context, aiqa_management_device_status_t *out_status);
    bool (*copy_public_config)(void *context, aiqa_management_public_config_t *out_config);
    /* Must copy the request into an owned asynchronous job before returning. */
    aiqa_management_result_t (*submit_wifi)(
        void *context,
        uint32_t operation_id,
        const aiqa_management_owned_wifi_update_t *update);
} aiqa_management_ports_t;

#define AIQA_MANAGEMENT_SERVICE_PRIVATE_SIZE (sizeof(aiqa_management_ports_t) + 64U)

typedef union {
    max_align_t _alignment;
    unsigned char _private[AIQA_MANAGEMENT_SERVICE_PRIVATE_SIZE];
} aiqa_management_service_t;

bool aiqa_management_service_init(
    aiqa_management_service_t *service,
    const aiqa_management_ports_t *ports);

aiqa_management_result_t aiqa_management_service_get_status(
    aiqa_management_service_t *service,
    const aiqa_management_security_context_t *access,
    aiqa_management_device_status_t *out_status);

aiqa_management_result_t aiqa_management_service_get_public_config(
    aiqa_management_service_t *service,
    const aiqa_management_security_context_t *access,
    aiqa_management_public_config_t *out_config);

aiqa_management_result_t aiqa_management_service_submit_wifi_update(
    aiqa_management_service_t *service,
    const aiqa_management_security_context_t *access,
    const aiqa_management_owned_wifi_update_t *update,
    uint32_t *out_operation_id);

bool aiqa_management_service_complete_wifi_update(
    aiqa_management_service_t *service,
    uint32_t operation_id,
    aiqa_management_result_t result);

bool aiqa_management_public_config_from_snapshot(
    const aiqa_config_snapshot_t *snapshot,
    aiqa_management_public_config_t *out_config);

void aiqa_management_owned_wifi_update_secure_clear(
    aiqa_management_owned_wifi_update_t *update);

const char *aiqa_management_result_name(aiqa_management_result_t result);

#ifdef __cplusplus
}
#endif
