#include "aiqa_asr_client.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls_crypto.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "aiqa_asr";

#define AIQA_ASR_HTTP_RESPONSE_MAX_LEN 4096
#define AIQA_ASR_BASE64_RAW_CHUNK_BYTES 768u
#define AIQA_ASR_BASE64_ENCODED_CHUNK_BYTES 1028u

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool overflow;
} aiqa_asr_response_buffer_t;

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

static esp_err_t asr_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->user_data == NULL) {
        return ESP_OK;
    }

    aiqa_asr_response_buffer_t *buffer = (aiqa_asr_response_buffer_t *)event->user_data;
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

static esp_err_t write_all(esp_http_client_handle_t client, const char *data, size_t length)
{
    if (client == NULL || (data == NULL && length > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t written_total = 0;
    while (written_total < length) {
        const size_t remaining = length - written_total;
        const int request_len = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        const int written = esp_http_client_write(client, data + written_total, request_len);
        if (written <= 0) {
            return ESP_FAIL;
        }
        written_total += (size_t)written;
    }
    return ESP_OK;
}

static aiqa_asr_status_t read_asr_response(
    esp_http_client_handle_t client,
    char *response_body,
    size_t response_body_size,
    aiqa_asr_result_t *result)
{
    if (client == NULL || response_body == NULL || response_body_size == 0 || result == NULL) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    response_body[0] = '\0';
    const int64_t content_length = esp_http_client_fetch_headers(client);
    result->http_status = esp_http_client_get_status_code(client);
    if (content_length >= (int64_t)response_body_size) {
        return AIQA_ASR_ERR_BUFFER_TOO_SMALL;
    }

    const int data_read = esp_http_client_read_response(client, response_body, (int)response_body_size - 1);
    if (data_read < 0) {
        return AIQA_ASR_ERR_PROVIDER;
    }
    response_body[data_read] = '\0';

    aiqa_asr_status_t status = aiqa_asr_status_from_http_status(result->http_status);
    if (status == AIQA_ASR_OK) {
        status = aiqa_asr_parse_transcript_text(response_body, result->text, sizeof(result->text));
    }
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

esp_err_t aiqa_asr_transcribe_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
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
    };
    esp_http_client_config_t http_config = {
        .url = endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_ASR_DEFAULT_TIMEOUT_MS,
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
        ret = esp_http_client_perform(client);
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

esp_err_t aiqa_asr_transcribe_pcm_wav_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const aiqa_asr_pcm_audio_t *audio,
    aiqa_asr_result_t *result)
{
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

    uint8_t wav_header[AIQA_ASR_WAV_HEADER_BYTES] = {0};
    asr_status = aiqa_asr_write_wav_header(
        wav_header,
        audio->sample_rate_hz,
        audio->bits_per_sample,
        audio->channels,
        audio->pcm_bytes);
    if (asr_status != AIQA_ASR_OK) {
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    char endpoint_url[AIQA_ASR_ENDPOINT_MAX_LEN] = {0};
    asr_status = aiqa_asr_build_endpoint_url(config->asr_base_url, endpoint_url, sizeof(endpoint_url));
    if (asr_status != AIQA_ASR_OK) {
        secure_zero(wav_header, sizeof(wav_header));
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    char prefix[AIQA_ASR_REQUEST_PART_MAX_LEN] = {0};
    char suffix[AIQA_ASR_REQUEST_PART_MAX_LEN] = {0};
    asr_status = aiqa_asr_build_data_uri_request_prefix_json(config, prefix, sizeof(prefix));
    if (asr_status == AIQA_ASR_OK) {
        asr_status = aiqa_asr_build_request_suffix_json(&options, suffix, sizeof(suffix));
    }
    const size_t encoded_len = aiqa_asr_base64_encoded_len(wav_bytes);
    if (asr_status != AIQA_ASR_OK || encoded_len == 0) {
        secure_zero(wav_header, sizeof(wav_header));
        result->status = asr_status != AIQA_ASR_OK ? asr_status : AIQA_ASR_ERR_BUFFER_TOO_SMALL;
        return ESP_ERR_INVALID_ARG;
    }
    const size_t post_len = strlen(prefix) + encoded_len + 1u + strlen(suffix);
    if (post_len > (size_t)INT_MAX) {
        secure_zero(wav_header, sizeof(wav_header));
        result->status = AIQA_ASR_ERR_BUFFER_TOO_SMALL;
        return ESP_ERR_INVALID_ARG;
    }

    char *response_body = (char *)malloc(AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    if (response_body == NULL) {
        secure_zero(wav_header, sizeof(wav_header));
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }
    response_body[0] = '\0';

    esp_http_client_config_t http_config = {
        .url = endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_ASR_DEFAULT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        secure_zero(wav_header, sizeof(wav_header));
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(response_body);
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    char auth_header[AIQA_MAX_API_KEY_LEN + 8] = {0};
    int written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    if (written < 0 || (size_t)written >= sizeof(auth_header)) {
        (void)esp_http_client_cleanup(client);
        secure_zero(auth_header, sizeof(auth_header));
        secure_zero(wav_header, sizeof(wav_header));
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(response_body);
        result->status = AIQA_ASR_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = set_common_headers(client, auth_header);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sending ASR WAV request: pcm_bytes=%u wav_bytes=%u",
                 (unsigned)audio->pcm_bytes,
                 (unsigned)wav_bytes);
        ret = esp_http_client_open(client, (int)post_len);
    }
    if (ret == ESP_OK) {
        ret = write_all(client, prefix, strlen(prefix));
    }

    uint8_t raw_chunk[AIQA_ASR_BASE64_RAW_CHUNK_BYTES] = {0};
    unsigned char encoded_chunk[AIQA_ASR_BASE64_ENCODED_CHUNK_BYTES] = {0};
    size_t wav_offset = 0;
    while (ret == ESP_OK && wav_offset < wav_bytes) {
        size_t raw_len = wav_bytes - wav_offset;
        if (raw_len > AIQA_ASR_BASE64_RAW_CHUNK_BYTES) {
            raw_len = AIQA_ASR_BASE64_RAW_CHUNK_BYTES;
        }
        copy_wav_virtual_chunk(wav_header, audio->pcm, wav_offset, raw_chunk, raw_len);
        size_t out_len = 0;
        const int b64_ret = esp_crypto_base64_encode(
            encoded_chunk,
            sizeof(encoded_chunk),
            &out_len,
            raw_chunk,
            raw_len);
        secure_zero(raw_chunk, sizeof(raw_chunk));
        if (b64_ret != 0 || out_len == 0 || out_len > sizeof(encoded_chunk)) {
            ret = ESP_FAIL;
            result->status = AIQA_ASR_ERR_PROVIDER;
            break;
        }
        ret = write_all(client, (const char *)encoded_chunk, out_len);
        secure_zero(encoded_chunk, sizeof(encoded_chunk));
        wav_offset += raw_len;
    }
    if (ret == ESP_OK) {
        ret = write_all(client, "\"", 1);
    }
    if (ret == ESP_OK) {
        ret = write_all(client, suffix, strlen(suffix));
    }

    if (ret == ESP_OK) {
        result->status = read_asr_response(client, response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN, result);
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
    secure_zero(auth_header, sizeof(auth_header));
    secure_zero(wav_header, sizeof(wav_header));
    secure_zero(raw_chunk, sizeof(raw_chunk));
    secure_zero(encoded_chunk, sizeof(encoded_chunk));
    secure_zero(prefix, sizeof(prefix));
    secure_zero(suffix, sizeof(suffix));
    secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    free(response_body);
    return ret == ESP_OK ? ESP_OK : ret;
}
