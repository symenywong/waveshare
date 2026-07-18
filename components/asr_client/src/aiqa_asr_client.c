#include "aiqa_asr_client.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls_crypto.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "aiqa_request_epoch.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "aiqa_asr";

#define AIQA_ASR_HTTP_RESPONSE_MAX_LEN 4096
#define AIQA_ASR_BASE64_RAW_CHUNK_BYTES 3072u
#define AIQA_ASR_BASE64_ENCODED_CHUNK_BYTES 4100u
#define AIQA_ASR_SOCKET_TIMEOUT_MS 8000u

static aiqa_request_epoch_t s_request_epoch = AIQA_REQUEST_EPOCH_INITIALIZER;
static portMUX_TYPE s_diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static aiqa_asr_diagnostics_t s_diagnostics = {
    .phase = AIQA_ASR_DIAGNOSTIC_IDLE,
    .status = AIQA_ASR_ERR_PROVIDER,
    .content_length = -1,
};

static void diagnostics_write_begin(void)
{
    portENTER_CRITICAL(&s_diagnostics_lock);
}

static void diagnostics_write_end(void)
{
    portEXIT_CRITICAL(&s_diagnostics_lock);
}

bool aiqa_asr_copy_diagnostics(aiqa_asr_diagnostics_t *out_diagnostics)
{
    if (out_diagnostics == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_diagnostics_lock);
    *out_diagnostics = s_diagnostics;
    portEXIT_CRITICAL(&s_diagnostics_lock);
    return true;
}

static void diagnostics_request(
    uint32_t request_epoch,
    size_t pcm_bytes,
    size_t post_bytes)
{
    diagnostics_write_begin();
    if (aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        s_diagnostics = (aiqa_asr_diagnostics_t){
            .request_epoch = request_epoch,
            .phase = AIQA_ASR_DIAGNOSTIC_REQUEST_READY,
            .status = AIQA_ASR_ERR_PROVIDER,
            .transport_status = ESP_OK,
            .content_length = -1,
            .pcm_bytes = pcm_bytes,
            .post_bytes = post_bytes,
            .response_limit = AIQA_ASR_HTTP_RESPONSE_MAX_LEN - 1U,
        };
    }
    diagnostics_write_end();
}

static void diagnostics_upload(
    uint32_t request_epoch,
    size_t uploaded_bytes,
    uint32_t upload_write_calls,
    esp_err_t transport_status,
    int socket_errno,
    uint32_t elapsed_ms)
{
    diagnostics_write_begin();
    if (s_diagnostics.request_epoch == request_epoch &&
        aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        s_diagnostics.phase = transport_status == ESP_OK
                                  ? AIQA_ASR_DIAGNOSTIC_UPLOAD_COMPLETE
                                  : AIQA_ASR_DIAGNOSTIC_UPLOAD_FAILED;
        s_diagnostics.uploaded_bytes = uploaded_bytes;
        s_diagnostics.upload_write_calls = upload_write_calls;
        s_diagnostics.transport_status = transport_status;
        s_diagnostics.socket_errno = socket_errno;
        s_diagnostics.elapsed_ms = elapsed_ms;
    }
    diagnostics_write_end();
}

static void diagnostics_headers(
    uint32_t request_epoch,
    int http_status,
    int64_t content_length,
    uint32_t header_wait_ms,
    uint32_t elapsed_ms)
{
    diagnostics_write_begin();
    if (s_diagnostics.request_epoch == request_epoch &&
        aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        s_diagnostics.phase = AIQA_ASR_DIAGNOSTIC_WAITING_HEADERS;
        s_diagnostics.http_status = http_status;
        s_diagnostics.content_length = content_length;
        s_diagnostics.header_wait_ms = header_wait_ms;
        s_diagnostics.elapsed_ms = elapsed_ms;
    }
    diagnostics_write_end();
}

static void diagnostics_response(
    uint32_t request_epoch,
    aiqa_asr_status_t status,
    size_t response_bytes,
    bool complete,
    uint32_t elapsed_ms)
{
    diagnostics_write_begin();
    if (s_diagnostics.request_epoch == request_epoch &&
        aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        s_diagnostics.phase = AIQA_ASR_DIAGNOSTIC_RESPONSE_COMPLETE;
        s_diagnostics.status = status;
        s_diagnostics.response_bytes = response_bytes;
        s_diagnostics.response_complete = complete;
        s_diagnostics.elapsed_ms = elapsed_ms;
    }
    diagnostics_write_end();
}

uint32_t aiqa_asr_request_epoch_capture(void)
{
    return aiqa_request_epoch_capture(&s_request_epoch);
}

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool overflow;
    uint32_t request_epoch;
} aiqa_asr_response_buffer_t;

typedef struct {
    uint8_t wav_header[AIQA_ASR_WAV_HEADER_BYTES];
    char endpoint_url[AIQA_ASR_ENDPOINT_MAX_LEN];
    char prefix[AIQA_ASR_REQUEST_PART_MAX_LEN];
    char suffix[AIQA_ASR_REQUEST_PART_MAX_LEN];
    char auth_header[AIQA_MAX_API_KEY_LEN + 8];
    uint8_t raw_chunk[AIQA_ASR_BASE64_RAW_CHUNK_BYTES];
    unsigned char encoded_chunk[AIQA_ASR_BASE64_ENCODED_CHUNK_BYTES];
} aiqa_asr_pcm_workspace_t;

void aiqa_asr_cancel_active_request(void)
{
    aiqa_request_epoch_cancel(&s_request_epoch);
    const uint32_t current_epoch = aiqa_asr_request_epoch_capture();
    diagnostics_write_begin();
    s_diagnostics = (aiqa_asr_diagnostics_t){
        .request_epoch = current_epoch,
        .phase = AIQA_ASR_DIAGNOSTIC_IDLE,
        .status = AIQA_ASR_ERR_PROVIDER,
        .content_length = -1,
    };
    diagnostics_write_end();
}

static void asr_result_reset(aiqa_asr_result_t *result)
{
    if (result == NULL) {
        return;
    }
    *result = (aiqa_asr_result_t){
        .status = AIQA_ASR_ERR_PROVIDER,
        .http_status = 0,
        .text = {0},
    };
}

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static void free_pcm_workspace(aiqa_asr_pcm_workspace_t *workspace)
{
    if (workspace == NULL) {
        return;
    }
    secure_zero(workspace, sizeof(*workspace));
    free(workspace);
}

static esp_err_t asr_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->user_data == NULL) {
        return ESP_OK;
    }

    aiqa_asr_response_buffer_t *buffer = (aiqa_asr_response_buffer_t *)event->user_data;
    if (!aiqa_request_epoch_is_current(&s_request_epoch, buffer->request_epoch)) {
        return ESP_FAIL;
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    const size_t chunk_len = (size_t)event->data_len;
    if (buffer->length + chunk_len >= buffer->capacity) {
        buffer->overflow = true;
        return ESP_OK;
    }

    (void)memcpy(buffer->data + buffer->length, event->data, chunk_len);
    buffer->length += chunk_len;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

static aiqa_asr_status_t status_from_transport(esp_err_t ret)
{
    if (ret == ESP_OK) {
        return AIQA_ASR_OK;
    }
    if (ret == ESP_ERR_TIMEOUT) {
        return AIQA_ASR_ERR_TIMEOUT;
    }
    return AIQA_ASR_ERR_PROVIDER;
}

static const char *select_asr_key(const aiqa_secret_config_t *secrets)
{
    if (secrets == NULL) {
        return NULL;
    }
    return secrets->asr_api_key[0] != '\0' ? secrets->asr_api_key : secrets->chat_api_key;
}

static esp_err_t set_common_headers(esp_http_client_handle_t client, const char *auth_header)
{
    esp_err_t ret = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Authorization", auth_header);
    }
    return ret;
}

static esp_err_t set_request_timeout(
    esp_http_client_handle_t client,
    int64_t request_started_us)
{
    const int64_t elapsed_ms = (esp_timer_get_time() - request_started_us) / 1000;
    if (elapsed_ms >= (int64_t)AIQA_ASR_DEFAULT_TIMEOUT_MS) {
        return ESP_ERR_TIMEOUT;
    }
    const int64_t remaining_ms = (int64_t)AIQA_ASR_DEFAULT_TIMEOUT_MS - elapsed_ms;
    const int timeout_ms = remaining_ms < (int64_t)AIQA_ASR_SOCKET_TIMEOUT_MS
                               ? (int)remaining_ms
                               : (int)AIQA_ASR_SOCKET_TIMEOUT_MS;
    return esp_http_client_set_timeout_ms(client, timeout_ms);
}

static esp_err_t write_all(
    esp_http_client_handle_t client,
    const char *data,
    size_t length,
    uint32_t request_epoch,
    size_t *uploaded_bytes,
    uint32_t *write_calls,
    int64_t request_started_us)
{
    if (client == NULL || (data == NULL && length > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t written_total = 0;
    while (written_total < length) {
        if (!aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
            return ESP_ERR_INVALID_STATE;
        }
        const esp_err_t timeout_ret = set_request_timeout(client, request_started_us);
        if (timeout_ret != ESP_OK) {
            return timeout_ret;
        }
        const size_t remaining = length - written_total;
        const int request_len = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        if (write_calls != NULL) {
            *write_calls += 1U;
        }
        const int written = esp_http_client_write(client, data + written_total, request_len);
        if (written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        if (written < 0) {
            return ESP_FAIL;
        }
        written_total += (size_t)written;
        if (uploaded_bytes != NULL) {
            *uploaded_bytes += (size_t)written;
        }
    }
    return ESP_OK;
}

static aiqa_asr_status_t read_asr_response(
    esp_http_client_handle_t client,
    char *response_body,
    size_t response_body_size,
    aiqa_asr_result_t *result,
    uint32_t request_epoch,
    int64_t request_started_us)
{
    if (client == NULL || response_body == NULL || response_body_size == 0 || result == NULL) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    response_body[0] = '\0';
    if (!aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        return AIQA_ASR_ERR_PROVIDER;
    }
    if (set_request_timeout(client, request_started_us) != ESP_OK) {
        const uint32_t elapsed_ms =
            (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
        diagnostics_response(
            request_epoch, AIQA_ASR_ERR_TIMEOUT, 0U, false, elapsed_ms);
        return AIQA_ASR_ERR_TIMEOUT;
    }
    const int64_t headers_started_us = esp_timer_get_time();
    diagnostics_headers(request_epoch, 0, -1, 0U,
                        (uint32_t)((headers_started_us - request_started_us) / 1000));
    const int64_t content_length = esp_http_client_fetch_headers(client);
    const char *header_result = content_length == -(int64_t)ESP_ERR_HTTP_EAGAIN
                                    ? "timeout"
                                    : (content_length < 0 ? "error" : "ok");
    result->http_status = esp_http_client_get_status_code(client);
    const uint32_t header_wait_ms =
        (uint32_t)((esp_timer_get_time() - headers_started_us) / 1000);
    const uint32_t headers_elapsed_ms =
        (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
    diagnostics_headers(
        request_epoch, result->http_status, content_length,
        header_wait_ms, headers_elapsed_ms);
    ESP_LOGI(TAG,
             "AIQA_DIAG asr_headers epoch=%u http=%d content_length=%lld header_result=%s header_wait_ms=%u elapsed_ms=%u",
             (unsigned)request_epoch,
             result->http_status,
             (long long)content_length,
             header_result,
             (unsigned)header_wait_ms,
             (unsigned)headers_elapsed_ms);
    if (content_length == -(int64_t)ESP_ERR_HTTP_EAGAIN) {
        const uint32_t elapsed_ms =
            (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
        diagnostics_response(
            request_epoch, AIQA_ASR_ERR_TIMEOUT, 0U, false, elapsed_ms);
        ESP_LOGI(TAG,
                 "AIQA_DIAG asr_response epoch=%u http=%d response_bytes=%u response_limit=%u complete=%d status=%s elapsed_ms=%u",
                 (unsigned)request_epoch, result->http_status, 0U,
                 (unsigned)(response_body_size - 1U), 0,
                 aiqa_asr_status_name(AIQA_ASR_ERR_TIMEOUT),
                 (unsigned)elapsed_ms);
        return AIQA_ASR_ERR_TIMEOUT;
    }
    if (content_length < 0) {
        const uint32_t elapsed_ms =
            (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
        diagnostics_response(
            request_epoch, AIQA_ASR_ERR_PROVIDER, 0U, false, elapsed_ms);
        ESP_LOGI(TAG,
                 "AIQA_DIAG asr_response epoch=%u http=%d response_bytes=%u response_limit=%u complete=%d status=%s elapsed_ms=%u",
                 (unsigned)request_epoch, result->http_status, 0U,
                 (unsigned)(response_body_size - 1U), 0,
                 aiqa_asr_status_name(AIQA_ASR_ERR_PROVIDER),
                 (unsigned)elapsed_ms);
        return AIQA_ASR_ERR_PROVIDER;
    }
    if (content_length >= (int64_t)response_body_size) {
        const uint32_t elapsed_ms =
            (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
        diagnostics_response(
            request_epoch, AIQA_ASR_ERR_BUFFER_TOO_SMALL, 0U, false, elapsed_ms);
        ESP_LOGI(TAG,
                 "AIQA_DIAG asr_response epoch=%u http=%d response_bytes=%u response_limit=%u complete=%d status=%s elapsed_ms=%u",
                 (unsigned)request_epoch, result->http_status, 0U,
                 (unsigned)(response_body_size - 1U), 0,
                 aiqa_asr_status_name(AIQA_ASR_ERR_BUFFER_TOO_SMALL),
                 (unsigned)elapsed_ms);
        return AIQA_ASR_ERR_BUFFER_TOO_SMALL;
    }

    size_t total_read = 0;
    while (total_read + 1U < response_body_size) {
        if (!aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
            return AIQA_ASR_ERR_PROVIDER;
        }
        if (set_request_timeout(client, request_started_us) != ESP_OK) {
            const uint32_t elapsed_ms =
                (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
            diagnostics_response(
                request_epoch, AIQA_ASR_ERR_TIMEOUT, total_read, false, elapsed_ms);
            return AIQA_ASR_ERR_TIMEOUT;
        }
        const int data_read = esp_http_client_read(
            client,
            response_body + total_read,
            (int)(response_body_size - total_read - 1U));
        if (data_read == -ESP_ERR_HTTP_EAGAIN) {
            const uint32_t elapsed_ms =
                (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
            diagnostics_response(
                request_epoch, AIQA_ASR_ERR_TIMEOUT, total_read, false, elapsed_ms);
            return AIQA_ASR_ERR_TIMEOUT;
        }
        if (data_read < 0) {
            const uint32_t elapsed_ms =
                (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
            diagnostics_response(
                request_epoch, AIQA_ASR_ERR_PROVIDER, total_read, false, elapsed_ms);
            ESP_LOGI(TAG,
                     "AIQA_DIAG asr_response epoch=%u http=%d response_bytes=%u response_limit=%u complete=%d status=%s elapsed_ms=%u",
                     (unsigned)request_epoch, result->http_status,
                     (unsigned)total_read, (unsigned)(response_body_size - 1U), 0,
                     aiqa_asr_status_name(AIQA_ASR_ERR_PROVIDER),
                     (unsigned)elapsed_ms);
            return AIQA_ASR_ERR_PROVIDER;
        }
        if (data_read == 0) {
            break;
        }
        total_read += (size_t)data_read;
    }
    response_body[total_read] = '\0';

    const bool complete = esp_http_client_is_complete_data_received(client);
    if (total_read + 1U == response_body_size && !complete) {
        const uint32_t elapsed_ms =
            (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
        diagnostics_response(
            request_epoch, AIQA_ASR_ERR_BUFFER_TOO_SMALL,
            total_read, false, elapsed_ms);
        ESP_LOGI(TAG,
                 "AIQA_DIAG asr_response epoch=%u http=%d response_bytes=%u response_limit=%u complete=%d status=%s elapsed_ms=%u",
                 (unsigned)request_epoch, result->http_status,
                 (unsigned)total_read, (unsigned)(response_body_size - 1U), 0,
                 aiqa_asr_status_name(AIQA_ASR_ERR_BUFFER_TOO_SMALL),
                 (unsigned)elapsed_ms);
        return AIQA_ASR_ERR_BUFFER_TOO_SMALL;
    }

    aiqa_asr_status_t status = aiqa_asr_status_from_http_status(result->http_status);
    if (status == AIQA_ASR_OK) {
        status = aiqa_asr_parse_transcript_text(response_body, result->text, sizeof(result->text));
    }
    const uint32_t response_elapsed_ms =
        (uint32_t)((esp_timer_get_time() - request_started_us) / 1000);
    diagnostics_response(
        request_epoch, status, total_read, complete, response_elapsed_ms);
    ESP_LOGI(TAG,
             "AIQA_DIAG asr_response epoch=%u http=%d response_bytes=%u response_limit=%u complete=%d status=%s elapsed_ms=%u",
             (unsigned)request_epoch, result->http_status,
             (unsigned)total_read, (unsigned)(response_body_size - 1U), complete ? 1 : 0,
             aiqa_asr_status_name(status),
             (unsigned)response_elapsed_ms);
    return status;
}

static void copy_wav_virtual_chunk(
    const uint8_t header[AIQA_ASR_WAV_HEADER_BYTES],
    const uint8_t *pcm,
    size_t offset,
    uint8_t *out,
    size_t length)
{
    size_t copied = 0;
    if (offset < AIQA_ASR_WAV_HEADER_BYTES) {
        const size_t header_available = AIQA_ASR_WAV_HEADER_BYTES - offset;
        const size_t header_copy = header_available < length ? header_available : length;
        (void)memcpy(out, header + offset, header_copy);
        copied += header_copy;
        offset += header_copy;
    }
    if (copied < length) {
        const size_t pcm_offset = offset - AIQA_ASR_WAV_HEADER_BYTES;
        (void)memcpy(out + copied, pcm + pcm_offset, length - copied);
    }
}

esp_err_t aiqa_asr_transcribe_once_with_epoch(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
    uint32_t request_epoch,
    aiqa_asr_result_t *result)
{
    asr_result_reset(result);
    if (config == NULL || secrets == NULL || audio_ref == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (aiqa_config_validate(config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(secrets) != AIQA_SECRET_OK) {
        result->status = AIQA_ASR_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    const char *api_key = select_asr_key(secrets);
    if (api_key == NULL || api_key[0] == '\0') {
        result->status = AIQA_ASR_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    char endpoint_url[AIQA_ASR_ENDPOINT_MAX_LEN] = {0};
    aiqa_asr_status_t asr_status = aiqa_asr_build_endpoint_url(
        config->asr_base_url,
        endpoint_url,
        sizeof(endpoint_url));
    if (asr_status != AIQA_ASR_OK) {
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_asr_options_t options = {
        .audio_ref = audio_ref,
        .language_hint = "zh",
        .enable_itn = true,
        .audio_source_kind = AIQA_AUDIO_SOURCE_PUBLIC_URL,
        .audio_bytes = 0,
    };
    char *request_body = (char *)malloc(AIQA_ASR_REQUEST_MAX_LEN);
    char *response_body = (char *)malloc(AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    if (request_body == NULL || response_body == NULL) {
        free(request_body);
        free(response_body);
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }
    request_body[0] = '\0';
    response_body[0] = '\0';

    asr_status = aiqa_asr_build_request_json(config, &options, request_body, AIQA_ASR_REQUEST_MAX_LEN);
    if (asr_status != AIQA_ASR_OK) {
        secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_asr_response_buffer_t response = {
        .data = response_body,
        .capacity = AIQA_ASR_HTTP_RESPONSE_MAX_LEN,
        .length = 0,
        .overflow = false,
        .request_epoch = request_epoch,
    };
    esp_http_client_config_t http_config = {
        .url = endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_ASR_SOCKET_TIMEOUT_MS,
        .event_handler = asr_http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    char auth_header[AIQA_MAX_API_KEY_LEN + 8] = {0};
    int written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    if (written < 0 || (size_t)written >= sizeof(auth_header)) {
        (void)esp_http_client_cleanup(client);
        secure_zero(auth_header, sizeof(auth_header));
        secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_ASR_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = set_common_headers(client, auth_header);
    if (ret == ESP_OK) {
        ret = esp_http_client_set_post_field(client, request_body, (int)strlen(request_body));
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sending ASR request to configured provider");
        ret = aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)
                  ? esp_http_client_perform(client)
                  : ESP_ERR_INVALID_STATE;
    }

    result->http_status = esp_http_client_get_status_code(client);
    if (ret != ESP_OK) {
        result->status = status_from_transport(ret);
        ESP_LOGE(TAG, "ASR transport failed: %s", esp_err_to_name(ret));
    } else if (response.overflow) {
        result->status = AIQA_ASR_ERR_BUFFER_TOO_SMALL;
        ESP_LOGE(TAG, "ASR response exceeded buffer");
    } else {
        result->status = aiqa_asr_status_from_http_status(result->http_status);
        if (result->status == AIQA_ASR_OK) {
            result->status = aiqa_asr_parse_transcript_text(response.data, result->text, sizeof(result->text));
        }
        if (result->status != AIQA_ASR_OK) {
            ESP_LOGE(TAG, "ASR provider failed: status=%s http=%d",
                     aiqa_asr_status_name(result->status),
                     result->http_status);
        }
    }

    (void)esp_http_client_cleanup(client);
    secure_zero(auth_header, sizeof(auth_header));
    secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
    secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    free(request_body);
    free(response_body);
    return ret == ESP_OK ? ESP_OK : ret;
}

esp_err_t aiqa_asr_transcribe_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
    aiqa_asr_result_t *result)
{
    return aiqa_asr_transcribe_once_with_epoch(
        config, secrets, audio_ref, aiqa_asr_request_epoch_capture(), result);
}

esp_err_t aiqa_asr_transcribe_pcm_wav_once_with_epoch(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const aiqa_asr_pcm_audio_t *audio,
    uint32_t request_epoch,
    aiqa_asr_result_t *result)
{
    const int64_t request_started_us = esp_timer_get_time();
    asr_result_reset(result);
    if (config == NULL || secrets == NULL || audio == NULL || audio->pcm == NULL ||
        audio->pcm_bytes == 0 || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (aiqa_config_validate(config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(secrets) != AIQA_SECRET_OK) {
        result->status = AIQA_ASR_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    const char *api_key = select_asr_key(secrets);
    if (api_key == NULL || api_key[0] == '\0') {
        result->status = AIQA_ASR_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    const size_t wav_bytes = aiqa_asr_wav_total_bytes(audio->pcm_bytes);
    if (wav_bytes == 0) {
        result->status = AIQA_ASR_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    aiqa_asr_options_t options = {
        .audio_ref = NULL,
        .language_hint = "zh",
        .enable_itn = true,
        .audio_source_kind = AIQA_AUDIO_SOURCE_DATA_URI,
        .audio_bytes = wav_bytes,
    };
    aiqa_asr_status_t asr_status = aiqa_asr_validate_audio_source(config, &options);
    if (asr_status != AIQA_ASR_OK) {
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_asr_pcm_workspace_t *workspace =
        (aiqa_asr_pcm_workspace_t *)calloc(1, sizeof(*workspace));
    if (workspace == NULL) {
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    asr_status = aiqa_asr_write_wav_header(
        workspace->wav_header,
        audio->sample_rate_hz,
        audio->bits_per_sample,
        audio->channels,
        audio->pcm_bytes);
    if (asr_status != AIQA_ASR_OK) {
        free_pcm_workspace(workspace);
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    asr_status = aiqa_asr_build_endpoint_url(
        config->asr_base_url,
        workspace->endpoint_url,
        sizeof(workspace->endpoint_url));
    if (asr_status != AIQA_ASR_OK) {
        free_pcm_workspace(workspace);
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    asr_status = aiqa_asr_build_data_uri_request_prefix_json(
        config,
        workspace->prefix,
        sizeof(workspace->prefix));
    if (asr_status == AIQA_ASR_OK) {
        asr_status = aiqa_asr_build_request_suffix_json(
            &options,
            workspace->suffix,
            sizeof(workspace->suffix));
    }
    const size_t encoded_len = aiqa_asr_base64_encoded_len(wav_bytes);
    if (asr_status != AIQA_ASR_OK || encoded_len == 0) {
        free_pcm_workspace(workspace);
        result->status = asr_status != AIQA_ASR_OK ? asr_status : AIQA_ASR_ERR_BUFFER_TOO_SMALL;
        return ESP_ERR_INVALID_ARG;
    }
    const size_t post_len =
        strlen(workspace->prefix) + encoded_len + 1u + strlen(workspace->suffix);
    if (post_len > (size_t)INT_MAX) {
        free_pcm_workspace(workspace);
        result->status = AIQA_ASR_ERR_BUFFER_TOO_SMALL;
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG,
             "AIQA_DIAG asr_request epoch=%u pcm_bytes=%u wav_bytes=%u post_bytes=%u",
             (unsigned)request_epoch,
             (unsigned)audio->pcm_bytes,
             (unsigned)wav_bytes,
             (unsigned)post_len);
    diagnostics_request(request_epoch, audio->pcm_bytes, post_len);

    char *response_body = (char *)malloc(AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    if (response_body == NULL) {
        free_pcm_workspace(workspace);
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }
    response_body[0] = '\0';

    esp_http_client_config_t http_config = {
        .url = workspace->endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_ASR_SOCKET_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        free_pcm_workspace(workspace);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(response_body);
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    int written = snprintf(
        workspace->auth_header,
        sizeof(workspace->auth_header),
        "Bearer %s",
        api_key);
    if (written < 0 || (size_t)written >= sizeof(workspace->auth_header)) {
        (void)esp_http_client_cleanup(client);
        free_pcm_workspace(workspace);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(response_body);
        result->status = AIQA_ASR_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = set_common_headers(client, workspace->auth_header);
    size_t uploaded_bytes = 0;
    uint32_t upload_write_calls = 0U;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sending ASR WAV request: pcm_bytes=%u wav_bytes=%u",
                 (unsigned)audio->pcm_bytes,
                 (unsigned)wav_bytes);
        ret = aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)
                  ? esp_http_client_open(client, (int)post_len)
                  : ESP_ERR_INVALID_STATE;
    }
    if (ret == ESP_OK &&
        !aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        ret = ESP_ERR_INVALID_STATE;
    }
    if (ret == ESP_OK) {
        ret = write_all(
            client, workspace->prefix, strlen(workspace->prefix), request_epoch,
            &uploaded_bytes, &upload_write_calls, request_started_us);
    }

    size_t wav_offset = 0;
    while (ret == ESP_OK &&
           aiqa_request_epoch_is_current(&s_request_epoch, request_epoch) &&
           wav_offset < wav_bytes) {
        size_t raw_len = wav_bytes - wav_offset;
        if (raw_len > AIQA_ASR_BASE64_RAW_CHUNK_BYTES) {
            raw_len = AIQA_ASR_BASE64_RAW_CHUNK_BYTES;
        }
        copy_wav_virtual_chunk(
            workspace->wav_header,
            audio->pcm,
            wav_offset,
            workspace->raw_chunk,
            raw_len);
        size_t out_len = 0;
        const int b64_ret = esp_crypto_base64_encode(
            workspace->encoded_chunk,
            sizeof(workspace->encoded_chunk),
            &out_len,
            workspace->raw_chunk,
            raw_len);
        secure_zero(workspace->raw_chunk, sizeof(workspace->raw_chunk));
        if (b64_ret != 0 || out_len == 0 || out_len > sizeof(workspace->encoded_chunk)) {
            ret = ESP_FAIL;
            result->status = AIQA_ASR_ERR_PROVIDER;
            break;
        }
        ret = write_all(
            client,
            (const char *)workspace->encoded_chunk,
            out_len,
            request_epoch,
            &uploaded_bytes,
            &upload_write_calls,
            request_started_us);
        secure_zero(workspace->encoded_chunk, sizeof(workspace->encoded_chunk));
        wav_offset += raw_len;
    }
    if (ret == ESP_OK &&
        !aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        ret = ESP_ERR_INVALID_STATE;
    }
    if (ret == ESP_OK) {
        ret = write_all(
            client, "\"", 1, request_epoch, &uploaded_bytes, &upload_write_calls,
            request_started_us);
    }
    if (ret == ESP_OK) {
        ret = write_all(
            client, workspace->suffix, strlen(workspace->suffix), request_epoch,
            &uploaded_bytes, &upload_write_calls, request_started_us);
    }
    const int socket_errno = ret == ESP_OK ? 0 : esp_http_client_get_errno(client);
    if (ret != ESP_OK && (socket_errno == ETIMEDOUT || socket_errno == EAGAIN)) {
        ret = ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG,
             "AIQA_DIAG asr_upload epoch=%u upload_bytes=%u write_calls=%u status=%s socket_errno=%d elapsed_ms=%u",
             (unsigned)request_epoch,
             (unsigned)uploaded_bytes,
             (unsigned)upload_write_calls,
             esp_err_to_name(ret),
             socket_errno,
             (unsigned)((esp_timer_get_time() - request_started_us) / 1000));
    diagnostics_upload(
        request_epoch,
        uploaded_bytes,
        upload_write_calls,
        ret,
        socket_errno,
        (uint32_t)((esp_timer_get_time() - request_started_us) / 1000));

    if (ret == ESP_OK &&
        aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        ret = set_request_timeout(client, request_started_us);
    }
    if (ret == ESP_OK &&
        aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)) {
        result->status = read_asr_response(
            client,
            response_body,
            AIQA_ASR_HTTP_RESPONSE_MAX_LEN,
            result,
            request_epoch,
            request_started_us);
        if (result->status != AIQA_ASR_OK) {
            ESP_LOGE(TAG, "ASR provider failed: status=%s http=%d",
                     aiqa_asr_status_name(result->status),
                     result->http_status);
        }
    } else {
        result->status = status_from_transport(ret);
        ESP_LOGE(TAG, "ASR WAV transport failed: %s", esp_err_to_name(ret));
    }

    (void)esp_http_client_close(client);
    (void)esp_http_client_cleanup(client);
    free_pcm_workspace(workspace);
    secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    free(response_body);
    return ret == ESP_OK ? ESP_OK : ret;
}

esp_err_t aiqa_asr_transcribe_pcm_wav_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const aiqa_asr_pcm_audio_t *audio,
    aiqa_asr_result_t *result)
{
    return aiqa_asr_transcribe_pcm_wav_once_with_epoch(
        config, secrets, audio, aiqa_asr_request_epoch_capture(), result);
}
