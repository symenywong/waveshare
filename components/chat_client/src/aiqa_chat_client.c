#include "aiqa_chat_client.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "aiqa_chat";

#define AIQA_CHAT_HTTP_RESPONSE_MAX_LEN 4096
#define AIQA_CHAT_STREAM_CARRY_MAX_LEN 1024
#define AIQA_CHAT_STREAM_DELTA_MAX_LEN 256

static StaticSemaphore_t s_active_client_mutex_storage;
static SemaphoreHandle_t s_active_client_mutex;
static esp_http_client_handle_t s_active_client;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool body_overflow;
    bool stream_overflow;
    bool stream;
    char carry[AIQA_CHAT_STREAM_CARRY_MAX_LEN];
    size_t carry_length;
    aiqa_chat_event_cb_t on_delta;
    void *user_ctx;
    aiqa_chat_result_t *result;
} aiqa_chat_response_buffer_t;

static SemaphoreHandle_t active_client_mutex(void)
{
    if (s_active_client_mutex == NULL) {
        s_active_client_mutex = xSemaphoreCreateMutexStatic(&s_active_client_mutex_storage);
    }
    return s_active_client_mutex;
}

static void set_active_client(esp_http_client_handle_t client)
{
    SemaphoreHandle_t mutex = active_client_mutex();
    if (mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        s_active_client = client;
        xSemaphoreGive(mutex);
    }
}

void aiqa_chat_cancel_active_request(void)
{
    SemaphoreHandle_t mutex = active_client_mutex();
    if (mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (s_active_client != NULL) {
        ESP_LOGW(TAG, "Cancelling active chat request");
        (void)esp_http_client_cancel_request(s_active_client);
    }
    xSemaphoreGive(mutex);
}

static void chat_result_reset(aiqa_chat_result_t *result)
{
    if (result == NULL) {
        return;
    }
    *result = (aiqa_chat_result_t){
        .status = AIQA_CHAT_ERR_PROVIDER,
        .http_status = 0,
        .text = {0},
    };
}

static void append_result_delta(aiqa_chat_response_buffer_t *buffer, const char *delta)
{
    if (buffer == NULL || buffer->result == NULL || delta == NULL || delta[0] == '\0') {
        return;
    }

    const size_t used = strlen(buffer->result->text);
    const size_t delta_len = strlen(delta);
    const size_t available = sizeof(buffer->result->text) - used - 1;
    if (available == 0) {
        return;
    }

    const size_t copy_len = delta_len < available ? delta_len : available;
    (void)memcpy(buffer->result->text + used, delta, copy_len);
    buffer->result->text[used + copy_len] = '\0';
    if (buffer->on_delta != NULL) {
        buffer->on_delta(delta, buffer->user_ctx);
    }
}

static void process_stream_event(aiqa_chat_response_buffer_t *buffer, const char *event_data, size_t event_len)
{
    if (buffer == NULL || event_data == NULL || event_len == 0) {
        return;
    }
    if (event_len >= AIQA_CHAT_STREAM_CARRY_MAX_LEN) {
        buffer->stream_overflow = true;
        return;
    }

    char event_text[AIQA_CHAT_STREAM_CARRY_MAX_LEN];
    (void)memcpy(event_text, event_data, event_len);
    event_text[event_len] = '\0';

    char delta[AIQA_CHAT_STREAM_DELTA_MAX_LEN] = {0};
    if (aiqa_chat_parse_stream_delta_text(event_text, delta, sizeof(delta)) == AIQA_CHAT_OK) {
        append_result_delta(buffer, delta);
    }
}

static char *find_stream_event_separator(char *data, size_t length, size_t *separator_len)
{
    if (data == NULL || separator_len == NULL) {
        return NULL;
    }

    for (size_t index = 0; index + 1 < length; ++index) {
        if (data[index] == '\n' && data[index + 1] == '\n') {
            *separator_len = 2;
            return data + index;
        }
        if (index + 3 < length &&
            data[index] == '\r' &&
            data[index + 1] == '\n' &&
            data[index + 2] == '\r' &&
            data[index + 3] == '\n') {
            *separator_len = 4;
            return data + index;
        }
    }

    return NULL;
}

static void process_stream_chunk(aiqa_chat_response_buffer_t *buffer, const char *chunk, size_t chunk_len)
{
    if (buffer == NULL || chunk == NULL || chunk_len == 0) {
        return;
    }
    if (buffer->carry_length + chunk_len >= sizeof(buffer->carry)) {
        buffer->stream_overflow = true;
        return;
    }

    (void)memcpy(buffer->carry + buffer->carry_length, chunk, chunk_len);
    buffer->carry_length += chunk_len;
    buffer->carry[buffer->carry_length] = '\0';

    size_t separator_len = 0;
    char *separator = find_stream_event_separator(buffer->carry, buffer->carry_length, &separator_len);
    while (separator != NULL) {
        const size_t event_len = (size_t)(separator - buffer->carry);
        process_stream_event(buffer, buffer->carry, event_len);

        const size_t consumed = event_len + separator_len;
        const size_t remaining = buffer->carry_length - consumed;
        if (remaining > 0) {
            (void)memmove(buffer->carry, buffer->carry + consumed, remaining);
        }
        buffer->carry_length = remaining;
        buffer->carry[buffer->carry_length] = '\0';
        separator = find_stream_event_separator(buffer->carry, buffer->carry_length, &separator_len);
    }
}

static void process_stream_remainder(aiqa_chat_response_buffer_t *buffer)
{
    if (buffer == NULL || buffer->carry_length == 0) {
        return;
    }

    process_stream_event(buffer, buffer->carry, buffer->carry_length);
    buffer->carry_length = 0;
    buffer->carry[0] = '\0';
}

static esp_err_t chat_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->user_data == NULL) {
        return ESP_OK;
    }

    aiqa_chat_response_buffer_t *buffer = (aiqa_chat_response_buffer_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    const size_t chunk_len = (size_t)event->data_len;
    if (buffer->length + chunk_len >= buffer->capacity) {
        buffer->body_overflow = true;
    } else {
        (void)memcpy(buffer->data + buffer->length, event->data, chunk_len);
        buffer->length += chunk_len;
        buffer->data[buffer->length] = '\0';
    }

    if (buffer->stream) {
        process_stream_chunk(buffer, (const char *)event->data, chunk_len);
    }
    return ESP_OK;
}

static aiqa_chat_status_t status_from_transport(esp_err_t ret)
{
    if (ret == ESP_OK) {
        return AIQA_CHAT_OK;
    }
    if (ret == ESP_ERR_TIMEOUT) {
        return AIQA_CHAT_ERR_TIMEOUT;
    }
    return AIQA_CHAT_ERR_PROVIDER;
}

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static esp_err_t chat_send_request(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    const char *assistant_profile_context,
    bool stream,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result)
{
    chat_result_reset(result);
    if (config == NULL || secrets == NULL || prompt == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (aiqa_config_validate(config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(secrets) != AIQA_SECRET_OK) {
        result->status = AIQA_CHAT_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    const aiqa_provider_caps_t *caps = aiqa_provider_caps_for(config->active_provider);
    if (stream && (caps == NULL || !caps->supports_chat_stream)) {
        result->status = AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER;
        return ESP_ERR_INVALID_ARG;
    }

    char endpoint_url[AIQA_CHAT_ENDPOINT_MAX_LEN] = {0};
    aiqa_chat_status_t chat_status = aiqa_chat_build_endpoint_url(
        config->base_url,
        endpoint_url,
        sizeof(endpoint_url));
    if (chat_status != AIQA_CHAT_OK) {
        result->status = chat_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_chat_options_t options = {
        .stream = stream,
        .hide_reasoning = config->hide_reasoning,
        .max_completion_tokens = config->max_completion_tokens,
        .response_language = response_language,
        .conversation_context = conversation_context,
        .assistant_profile_context = assistant_profile_context,
    };
    char *request_body = (char *)malloc(AIQA_CHAT_REQUEST_MAX_LEN);
    char *response_body = (char *)malloc(AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
    if (request_body == NULL || response_body == NULL) {
        free(request_body);
        free(response_body);
        result->status = AIQA_CHAT_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }
    request_body[0] = '\0';
    response_body[0] = '\0';

    chat_status = aiqa_chat_build_request_json(
        config,
        &options,
        prompt,
        request_body,
        AIQA_CHAT_REQUEST_MAX_LEN);
    if (chat_status != AIQA_CHAT_OK) {
        secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = chat_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_chat_response_buffer_t response = {
        .data = response_body,
        .capacity = AIQA_CHAT_HTTP_RESPONSE_MAX_LEN,
        .length = 0,
        .body_overflow = false,
        .stream_overflow = false,
        .stream = stream,
        .carry = {0},
        .carry_length = 0,
        .on_delta = on_delta,
        .user_ctx = user_ctx,
        .result = result,
    };
    esp_http_client_config_t http_config = {
        .url = endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_CHAT_DEFAULT_TIMEOUT_MS,
        .event_handler = chat_http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_CHAT_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    char auth_header[AIQA_MAX_API_KEY_LEN + 8] = {0};
    int written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", secrets->chat_api_key);
    if (written < 0 || (size_t)written >= sizeof(auth_header)) {
        (void)esp_http_client_cleanup(client);
        secure_zero(auth_header, sizeof(auth_header));
        secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_CHAT_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (ret == ESP_OK && stream) {
        ret = esp_http_client_set_header(client, "Accept", "text/event-stream");
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Authorization", auth_header);
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_post_field(client, request_body, (int)strlen(request_body));
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sending chat request to configured provider");
        set_active_client(client);
        ret = esp_http_client_perform(client);
        set_active_client(NULL);
    }

    result->http_status = esp_http_client_get_status_code(client);
    if (ret != ESP_OK) {
        result->status = status_from_transport(ret);
        ESP_LOGE(TAG, "Chat transport failed: %s", esp_err_to_name(ret));
    } else if (!stream && response.body_overflow) {
        result->status = AIQA_CHAT_ERR_BUFFER_TOO_SMALL;
        ESP_LOGE(TAG, "Chat response exceeded buffer");
    } else {
        result->status = aiqa_chat_status_from_http_status(result->http_status);
        if (result->status == AIQA_CHAT_OK) {
            if (stream) {
                process_stream_remainder(&response);
                if (response.stream_overflow) {
                    result->status = AIQA_CHAT_ERR_BUFFER_TOO_SMALL;
                } else if (result->text[0] == '\0') {
                    result->status = AIQA_CHAT_ERR_PARSE;
                }
            } else {
                result->status = aiqa_chat_parse_response_text(response.data, result->text, sizeof(result->text));
            }
        }
        if (result->status != AIQA_CHAT_OK) {
            ESP_LOGE(TAG, "Chat provider failed: status=%s http=%d",
                     aiqa_chat_status_name(result->status),
                     result->http_status);
        }
    }

    (void)esp_http_client_cleanup(client);
    secure_zero(auth_header, sizeof(auth_header));
    secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
    secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
    free(request_body);
    free(response_body);
    return ret == ESP_OK ? ESP_OK : ret;
}

esp_err_t aiqa_chat_send_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    aiqa_chat_result_t *result)
{
    return aiqa_chat_send_once_with_language(config, secrets, prompt, NULL, result);
}

esp_err_t aiqa_chat_send_once_with_language(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    aiqa_chat_result_t *result)
{
    return aiqa_chat_send_once_with_context(
        config,
        secrets,
        prompt,
        response_language,
        NULL,
        result);
}

esp_err_t aiqa_chat_send_once_with_context(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    aiqa_chat_result_t *result)
{
    return aiqa_chat_send_once_with_contexts(
        config,
        secrets,
        prompt,
        response_language,
        conversation_context,
        NULL,
        result);
}

esp_err_t aiqa_chat_send_once_with_contexts(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    const char *assistant_profile_context,
    aiqa_chat_result_t *result)
{
    return chat_send_request(
        config,
        secrets,
        prompt,
        response_language,
        conversation_context,
        assistant_profile_context,
        false,
        NULL,
        NULL,
        result);
}

esp_err_t aiqa_chat_send_streaming(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result)
{
    return aiqa_chat_send_streaming_with_language(
        config,
        secrets,
        prompt,
        NULL,
        on_delta,
        user_ctx,
        result);
}

esp_err_t aiqa_chat_send_streaming_with_language(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result)
{
    return aiqa_chat_send_streaming_with_context(
        config,
        secrets,
        prompt,
        response_language,
        NULL,
        on_delta,
        user_ctx,
        result);
}

esp_err_t aiqa_chat_send_streaming_with_context(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result)
{
    return aiqa_chat_send_streaming_with_contexts(
        config,
        secrets,
        prompt,
        response_language,
        conversation_context,
        NULL,
        on_delta,
        user_ctx,
        result);
}

esp_err_t aiqa_chat_send_streaming_with_contexts(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    const char *assistant_profile_context,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result)
{
    return chat_send_request(
        config,
        secrets,
        prompt,
        response_language,
        conversation_context,
        assistant_profile_context,
        true,
        on_delta,
        user_ctx,
        result);
}
