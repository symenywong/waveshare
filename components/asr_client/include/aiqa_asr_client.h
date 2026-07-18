#pragma once

#include "aiqa_asr_protocol.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_ASR_DEFAULT_TIMEOUT_MS 30000u

typedef struct {
    aiqa_asr_status_t status;
    int http_status;
    char text[AIQA_ASR_RESPONSE_TEXT_MAX_LEN];
} aiqa_asr_result_t;

typedef struct {
    const uint8_t *pcm;
    size_t pcm_bytes;
    uint32_t sample_rate_hz;
    uint16_t bits_per_sample;
    uint16_t channels;
} aiqa_asr_pcm_audio_t;

typedef enum {
    AIQA_ASR_DIAGNOSTIC_IDLE = 0,
    AIQA_ASR_DIAGNOSTIC_REQUEST_READY,
    AIQA_ASR_DIAGNOSTIC_UPLOAD_COMPLETE,
    AIQA_ASR_DIAGNOSTIC_WAITING_HEADERS,
    AIQA_ASR_DIAGNOSTIC_RESPONSE_COMPLETE,
    AIQA_ASR_DIAGNOSTIC_UPLOAD_FAILED,
} aiqa_asr_diagnostic_phase_t;

/* Fixed scalar projection only: never add request, response, or transcript text. */
typedef struct {
    uint32_t request_epoch;
    aiqa_asr_diagnostic_phase_t phase;
    aiqa_asr_status_t status;
    int http_status;
    esp_err_t transport_status;
    int socket_errno;
    int64_t content_length;
    size_t pcm_bytes;
    size_t post_bytes;
    size_t uploaded_bytes;
    uint32_t upload_write_calls;
    size_t response_bytes;
    size_t response_limit;
    uint32_t header_wait_ms;
    uint32_t elapsed_ms;
    bool response_complete;
} aiqa_asr_diagnostics_t;

esp_err_t aiqa_asr_transcribe_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
    aiqa_asr_result_t *result);

esp_err_t aiqa_asr_transcribe_once_with_epoch(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
    uint32_t request_epoch,
    aiqa_asr_result_t *result);

esp_err_t aiqa_asr_transcribe_pcm_wav_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const aiqa_asr_pcm_audio_t *audio,
    aiqa_asr_result_t *result);

esp_err_t aiqa_asr_transcribe_pcm_wav_once_with_epoch(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const aiqa_asr_pcm_audio_t *audio,
    uint32_t request_epoch,
    aiqa_asr_result_t *result);

uint32_t aiqa_asr_request_epoch_capture(void);
void aiqa_asr_cancel_active_request(void);
bool aiqa_asr_copy_diagnostics(aiqa_asr_diagnostics_t *out_diagnostics);

#ifdef __cplusplus
}
#endif
