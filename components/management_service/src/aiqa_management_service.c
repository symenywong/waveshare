#include "aiqa_management_service.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    aiqa_management_ports_t ports;
    atomic_uint latest_operation_id;
    atomic_uint latest_operation_state;
    atomic_uint latest_operation_result;
    atomic_uint operation_sequence;
    atomic_uint next_operation_id;
    atomic_bool operation_reserved;
    atomic_bool operation_completing;
    bool initialized;
} aiqa_management_service_impl_t;

_Static_assert(
    sizeof(aiqa_management_service_impl_t) <= sizeof(aiqa_management_service_t),
    "AIQA_MANAGEMENT_SERVICE_PRIVATE_SIZE is too small");

static aiqa_management_service_impl_t *service_impl(aiqa_management_service_t *service)
{
    return (aiqa_management_service_impl_t *)(void *)service;
}

static void secure_zero(void *value, size_t value_size)
{
    volatile unsigned char *bytes = value;
    while (value_size > 0) {
        *bytes++ = 0;
        --value_size;
    }
}

static void begin_operation_write(aiqa_management_service_impl_t *service)
{
    (void)atomic_fetch_add_explicit(
        &service->operation_sequence, 1U, memory_order_acq_rel);
}

static void end_operation_write(aiqa_management_service_impl_t *service)
{
    (void)atomic_fetch_add_explicit(
        &service->operation_sequence, 1U, memory_order_release);
}

static bool copy_latest_operation(
    const aiqa_management_service_impl_t *service,
    aiqa_management_operation_t *out_operation)
{
    if (out_operation == NULL) {
        return false;
    }
    for (unsigned attempt = 0; attempt < 64U; ++attempt) {
        const uint32_t before =
            atomic_load_explicit(&service->operation_sequence, memory_order_acquire);
        if ((before & 1U) != 0U) {
            continue;
        }
        aiqa_management_operation_t operation = {0};
        operation.id = atomic_load_explicit(
            &service->latest_operation_id, memory_order_relaxed);
        operation.state = (aiqa_management_operation_state_t)atomic_load_explicit(
            &service->latest_operation_state, memory_order_relaxed);
        operation.result = (aiqa_management_result_t)atomic_load_explicit(
            &service->latest_operation_result, memory_order_relaxed);
        const uint32_t after =
            atomic_load_explicit(&service->operation_sequence, memory_order_acquire);
        if (before == after && (after & 1U) == 0U) {
            *out_operation = operation;
            return true;
        }
    }
    return false;
}

static void store_operation(
    aiqa_management_service_impl_t *service,
    aiqa_management_operation_t operation)
{
    begin_operation_write(service);
    atomic_store_explicit(&service->latest_operation_id, operation.id, memory_order_relaxed);
    atomic_store_explicit(
        &service->latest_operation_result, (unsigned)operation.result, memory_order_relaxed);
    atomic_store_explicit(
        &service->latest_operation_state, (unsigned)operation.state, memory_order_relaxed);
    end_operation_write(service);
}

static size_t bounded_strlen(const char *value, size_t capacity)
{
    size_t length = 0;
    if (value == NULL) {
        return capacity;
    }
    while (length < capacity && value[length] != '\0') {
        ++length;
    }
    return length;
}

static bool update_is_valid(const aiqa_management_owned_wifi_update_t *update)
{
    if (update == NULL || update->base_revision == 0) {
        return false;
    }
    const size_t ssid_length = bounded_strlen(update->ssid, sizeof(update->ssid));
    const size_t password_length = bounded_strlen(update->password, sizeof(update->password));
    if (ssid_length == 0 || ssid_length >= sizeof(update->ssid) ||
        password_length >= sizeof(update->password)) {
        return false;
    }
    switch (update->password_action) {
    case AIQA_WIFI_PASSWORD_KEEP:
    case AIQA_WIFI_PASSWORD_CLEAR:
        return password_length == 0;
    case AIQA_WIFI_PASSWORD_REPLACE:
        return password_length >= 8 && password_length <= 63;
    default:
        return false;
    }
}

static bool ports_are_valid(const aiqa_management_ports_t *ports)
{
    return ports != NULL && ports->copy_status != NULL &&
           ports->authorize != NULL && ports->copy_public_config != NULL &&
           ports->submit_wifi != NULL;
}

bool aiqa_management_service_init(
    aiqa_management_service_t *service,
    const aiqa_management_ports_t *ports)
{
    if (service == NULL || !ports_are_valid(ports)) {
        return false;
    }
    aiqa_management_service_impl_t *impl = service_impl(service);
    secure_zero(impl, sizeof(*impl));
    impl->ports = *ports;
    atomic_init(&impl->latest_operation_id, 0);
    atomic_init(&impl->latest_operation_state, AIQA_MANAGEMENT_OPERATION_NONE);
    atomic_init(&impl->latest_operation_result, AIQA_MANAGEMENT_OK);
    atomic_init(&impl->operation_sequence, 0);
    atomic_init(&impl->next_operation_id, 1);
    atomic_init(&impl->operation_reserved, false);
    atomic_init(&impl->operation_completing, false);
    impl->initialized = true;
    return true;
}

aiqa_management_result_t aiqa_management_service_get_status(
    aiqa_management_service_t *service,
    const aiqa_management_security_context_t *access,
    aiqa_management_device_status_t *out_status)
{
    if (out_status != NULL) {
        secure_zero(out_status, sizeof(*out_status));
    }
    if (service == NULL || access == NULL || out_status == NULL) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    aiqa_management_service_impl_t *impl = service_impl(service);
    if (!impl->initialized) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    if (!impl->ports.authorize(
            impl->ports.context, access->session_id, AIQA_MANAGEMENT_CAPABILITY_READ)) {
        return AIQA_MANAGEMENT_FORBIDDEN;
    }
    aiqa_management_device_status_t status = {0};
    if (!impl->ports.copy_status(impl->ports.context, &status)) {
        secure_zero(&status, sizeof(status));
        return AIQA_MANAGEMENT_NOT_READY;
    }
    if (!copy_latest_operation(impl, &status.latest_operation)) {
        secure_zero(&status, sizeof(status));
        return AIQA_MANAGEMENT_BUSY;
    }
    *out_status = status;
    secure_zero(&status, sizeof(status));
    return AIQA_MANAGEMENT_OK;
}

aiqa_management_result_t aiqa_management_service_get_public_config(
    aiqa_management_service_t *service,
    const aiqa_management_security_context_t *access,
    aiqa_management_public_config_t *out_config)
{
    if (out_config != NULL) {
        secure_zero(out_config, sizeof(*out_config));
    }
    if (service == NULL || access == NULL || out_config == NULL) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    aiqa_management_service_impl_t *impl = service_impl(service);
    if (!impl->initialized) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    if (!impl->ports.authorize(
            impl->ports.context, access->session_id, AIQA_MANAGEMENT_CAPABILITY_READ)) {
        return AIQA_MANAGEMENT_FORBIDDEN;
    }
    const bool copied = impl->ports.copy_public_config(impl->ports.context, out_config);
    return copied ? AIQA_MANAGEMENT_OK : AIQA_MANAGEMENT_NOT_READY;
}

aiqa_management_result_t aiqa_management_service_submit_wifi_update(
    aiqa_management_service_t *service,
    const aiqa_management_security_context_t *access,
    const aiqa_management_owned_wifi_update_t *update,
    uint32_t *out_operation_id)
{
    if (out_operation_id != NULL) {
        *out_operation_id = 0;
    }
    if (service == NULL || access == NULL || update == NULL || out_operation_id == NULL) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    aiqa_management_service_impl_t *impl = service_impl(service);
    if (!impl->initialized) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    if (!impl->ports.authorize(
            impl->ports.context,
            access->session_id,
            AIQA_MANAGEMENT_CAPABILITY_MANAGE_CONFIG)) {
        return AIQA_MANAGEMENT_FORBIDDEN;
    }
    if (!update_is_valid(update)) {
        return AIQA_MANAGEMENT_INVALID_REQUEST;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &impl->operation_reserved,
            &expected,
            true,
            memory_order_acq_rel,
            memory_order_relaxed)) {
        return AIQA_MANAGEMENT_BUSY;
    }
    uint32_t operation_id = atomic_fetch_add_explicit(
        &impl->next_operation_id, 1U, memory_order_relaxed);
    if (operation_id == 0) {
        operation_id = atomic_fetch_add_explicit(
            &impl->next_operation_id, 1U, memory_order_relaxed);
    }
    store_operation(impl, (aiqa_management_operation_t){
        .id = operation_id,
        .state = AIQA_MANAGEMENT_OPERATION_PENDING,
        .result = AIQA_MANAGEMENT_OK,
    });
    const aiqa_management_result_t submit_result =
        impl->ports.submit_wifi(impl->ports.context, operation_id, update);
    if (submit_result != AIQA_MANAGEMENT_OK) {
        store_operation(impl, (aiqa_management_operation_t){0});
        atomic_store_explicit(&impl->operation_reserved, false, memory_order_release);
        return submit_result;
    }
    *out_operation_id = operation_id;
    return AIQA_MANAGEMENT_OK;
}

bool aiqa_management_service_complete_wifi_update(
    aiqa_management_service_t *service,
    uint32_t operation_id,
    aiqa_management_result_t result)
{
    if (service == NULL || operation_id == 0) {
        return false;
    }
    aiqa_management_service_impl_t *impl = service_impl(service);
    bool expected_completing = false;
    if (!impl->initialized ||
        !atomic_load_explicit(&impl->operation_reserved, memory_order_acquire) ||
        !atomic_compare_exchange_strong_explicit(
            &impl->operation_completing,
            &expected_completing,
            true,
            memory_order_acq_rel,
            memory_order_relaxed)) {
        return false;
    }
    aiqa_management_operation_t current = {0};
    if (!copy_latest_operation(impl, &current) ||
        current.state != AIQA_MANAGEMENT_OPERATION_PENDING || current.id != operation_id) {
        atomic_store_explicit(&impl->operation_completing, false, memory_order_release);
        return false;
    }
    store_operation(impl, (aiqa_management_operation_t){
        .id = operation_id,
        .state = result == AIQA_MANAGEMENT_OK ? AIQA_MANAGEMENT_OPERATION_SUCCEEDED
                                              : AIQA_MANAGEMENT_OPERATION_FAILED,
        .result = result,
    });
    atomic_store_explicit(&impl->operation_completing, false, memory_order_release);
    atomic_store_explicit(&impl->operation_reserved, false, memory_order_release);
    return true;
}

bool aiqa_management_public_config_from_snapshot(
    const aiqa_config_snapshot_t *snapshot,
    aiqa_management_public_config_t *out_config)
{
    if (out_config != NULL) {
        secure_zero(out_config, sizeof(*out_config));
    }
    if (snapshot == NULL || out_config == NULL || !snapshot->namespace_found ||
        snapshot->revision == 0 || aiqa_config_validate(&snapshot->config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(&snapshot->secrets) != AIQA_SECRET_OK) {
        return false;
    }
    aiqa_management_public_config_t projected = {
        .revision = snapshot->revision,
        .stream = snapshot->config.stream,
        .hide_reasoning = snapshot->config.hide_reasoning,
        .max_completion_tokens = snapshot->config.max_completion_tokens,
        .has_chat_api_key = snapshot->secrets.chat_api_key[0] != '\0',
        .has_asr_api_key = snapshot->secrets.asr_api_key[0] != '\0',
        .user_prefs = snapshot->user_prefs,
    };
    if (!aiqa_config_build_public_wifi_view(
            &snapshot->secrets, snapshot->revision, &projected.wifi)) {
        secure_zero(&projected, sizeof(projected));
        return false;
    }
    (void)snprintf(projected.chat_provider, sizeof(projected.chat_provider), "%s",
                   snapshot->config.active_provider);
    (void)snprintf(projected.chat_model, sizeof(projected.chat_model), "%s",
                   snapshot->config.model);
    (void)snprintf(projected.asr_provider, sizeof(projected.asr_provider), "%s",
                   snapshot->config.asr_provider);
    (void)snprintf(projected.asr_model, sizeof(projected.asr_model), "%s",
                   snapshot->config.asr_model);
    (void)snprintf(projected.tts_provider, sizeof(projected.tts_provider), "%s",
                   snapshot->config.tts_provider);
    (void)snprintf(projected.tts_model, sizeof(projected.tts_model), "%s",
                   snapshot->config.tts_model);
    (void)snprintf(projected.tts_voice, sizeof(projected.tts_voice), "%s",
                   snapshot->config.tts_voice);
    *out_config = projected;
    secure_zero(&projected, sizeof(projected));
    return true;
}

void aiqa_management_owned_wifi_update_secure_clear(
    aiqa_management_owned_wifi_update_t *update)
{
    if (update != NULL) {
        secure_zero(update, sizeof(*update));
    }
}

const char *aiqa_management_result_name(aiqa_management_result_t result)
{
    switch (result) {
    case AIQA_MANAGEMENT_OK: return "OK";
    case AIQA_MANAGEMENT_INVALID_REQUEST: return "INVALID_REQUEST";
    case AIQA_MANAGEMENT_FORBIDDEN: return "FORBIDDEN";
    case AIQA_MANAGEMENT_NOT_READY: return "NOT_READY";
    case AIQA_MANAGEMENT_REVISION_CONFLICT: return "REVISION_CONFLICT";
    case AIQA_MANAGEMENT_REVISION_EXHAUSTED: return "REVISION_EXHAUSTED";
    case AIQA_MANAGEMENT_BUSY: return "BUSY";
    case AIQA_MANAGEMENT_WIFI_UNREACHABLE_ROLLED_BACK: return "WIFI_UNREACHABLE_ROLLED_BACK";
    case AIQA_MANAGEMENT_PERSISTENCE_FAILED_ROLLED_BACK: return "PERSISTENCE_FAILED_ROLLED_BACK";
    case AIQA_MANAGEMENT_RECOVERY_REQUIRED: return "RECOVERY_REQUIRED";
    case AIQA_MANAGEMENT_CANCELLED: return "CANCELLED";
    case AIQA_MANAGEMENT_INTERNAL_ERROR: return "INTERNAL_ERROR";
    default: return "UNKNOWN";
    }
}
