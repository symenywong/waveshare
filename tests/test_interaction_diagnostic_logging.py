from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/app_runtime/src/aiqa_runtime.c"
ASR = ROOT / "components/asr_client/src/aiqa_asr_client.c"
CHAT = ROOT / "components/chat_client/src/aiqa_chat_client.c"
TTS = ROOT / "components/tts_client/src/aiqa_tts_client.c"
PROTOCOL = ROOT / "components/management_transport/src/aiqa_management_protocol.c"
USB = ROOT / "components/management_transport/src/aiqa_usb_management.c"
BOARD = ROOT / "components/board_wave_175c/src/board_wave_175c.c"


def test_runtime_logs_generation_state_elapsed_and_recording_metrics():
    source = RUNTIME.read_text(encoding="utf-8")

    assert "AIQA_DIAG state" in source
    assert "generation=%u" in source
    assert "state_elapsed_ms=%u" in source
    assert "AIQA_DIAG recording" in source
    assert "duration_ms=%u" in source
    assert "pcm_bytes=%u" in source
    assert "peak=%d" in source
    assert "AIQA_DIAG asr_result" in source
    assert "transcript_bytes=%u" in source
    assert "transport=%s" in source
    assert "AIQA_DIAG ui_render" in source
    assert "desired_state=%s" in source
    assert "page_status=%s" in source
    assert "render=%s" in source
    assert "dirty=%d" in source


def test_runtime_management_ready_flag_is_cross_task_atomic():
    source = RUNTIME.read_text(encoding="utf-8")

    assert "static atomic_bool s_management_service_ready" in source
    assert "atomic_load_explicit(&s_management_service_ready, memory_order_acquire)" in source
    assert "atomic_store_explicit(&s_management_service_ready, true, memory_order_release)" in source


def test_asr_logs_upload_response_capacity_and_total_timing():
    source = ASR.read_text(encoding="utf-8")

    assert "AIQA_DIAG asr_request" in source
    assert "wav_bytes=%u" in source
    assert "post_bytes=%u" in source
    assert "AIQA_DIAG asr_upload" in source
    assert "upload_bytes=%u" in source
    assert "AIQA_DIAG asr_headers" in source
    assert "content_length=%lld" in source
    assert "header_result=%s" in source
    assert "header_wait_ms=%u" in source
    assert "content_length == -(int64_t)ESP_ERR_HTTP_EAGAIN" in source
    assert "AIQA_ASR_ERR_TIMEOUT" in source.split(
        "const int64_t content_length = esp_http_client_fetch_headers", maxsplit=1
    )[1].split("if (content_length < 0)", maxsplit=1)[0]
    assert "AIQA_DIAG asr_response" in source
    assert "response_bytes=%u" in source
    assert "response_limit=%u" in source
    assert "complete=%d" in source
    assert "elapsed_ms=%u" in source
    assert "size_t uploaded_bytes = 0" in source
    assert "&uploaded_bytes" in source
    assert "esp_http_client_get_errno(client)" in source
    assert "upload_write_calls" in source
    assert "written == 0" in source
    assert "return ESP_ERR_TIMEOUT" in source.split("written == 0", maxsplit=1)[1]
    assert "AIQA_ASR_DIAGNOSTIC_UPLOAD_FAILED" in source
    assert '\\"socketErrno\\"' in PROTOCOL.read_text(encoding="utf-8")
    assert '\\"uploadWriteCalls\\"' in PROTOCOL.read_text(encoding="utf-8")


def test_asr_upload_uses_tls_efficient_chunks_and_bounded_longer_timeout():
    source = ASR.read_text(encoding="utf-8")

    assert "#define AIQA_ASR_BASE64_RAW_CHUNK_BYTES 3072u" in source
    assert "#define AIQA_ASR_SOCKET_TIMEOUT_MS 8000u" in source
    assert "AIQA_ASR_DEFAULT_TIMEOUT_MS" in source
    assert "esp_http_client_set_timeout_ms" in source
    assert "data_read == -ESP_ERR_HTTP_EAGAIN" in source
    assert source.count("set_request_timeout(client, request_started_us)") >= 3


def test_asr_chunked_response_at_capacity_is_reported_as_too_large():
    source = ASR.read_text(encoding="utf-8")
    response_reader = source.split("static aiqa_asr_status_t read_asr_response", maxsplit=1)[1].split(
        "static void copy_wav_virtual_chunk", maxsplit=1
    )[0]

    assert "esp_http_client_is_complete_data_received" in response_reader
    assert "AIQA_ASR_ERR_BUFFER_TOO_SMALL" in response_reader


def test_asr_snapshot_marks_waiting_headers_before_blocking_fetch():
    source = ASR.read_text(encoding="utf-8")
    reader = source.split("static aiqa_asr_status_t read_asr_response", maxsplit=1)[1]

    assert reader.index("diagnostics_headers(request_epoch, 0, -1") < reader.index(
        "esp_http_client_fetch_headers(client)"
    )
    assert "portENTER_CRITICAL(&s_diagnostics_lock)" in source
    assert "portEXIT_CRITICAL(&s_diagnostics_lock)" in source
    assert source.count(
        "aiqa_request_epoch_is_current(&s_request_epoch, request_epoch)"
    ) >= 8
    cancel = source.split("void aiqa_asr_cancel_active_request", maxsplit=1)[1].split(
        "static void asr_result_reset", maxsplit=1
    )[0]
    assert "AIQA_ASR_DIAGNOSTIC_IDLE" in cancel
    assert "current_epoch" in cancel

    request_snapshot = source.split("static void diagnostics_request", maxsplit=1)[1].split(
        "static void diagnostics_upload", maxsplit=1
    )[0]
    assert request_snapshot.index("diagnostics_write_begin()") < request_snapshot.index(
        "aiqa_request_epoch_is_current"
    ) < request_snapshot.index("s_diagnostics =") < request_snapshot.index(
        "diagnostics_write_end()"
    )


def test_usb_hello_exposes_only_fixed_scalar_diagnostics():
    protocol = PROTOCOL.read_text(encoding="utf-8")
    usb = USB.read_text(encoding="utf-8")
    diagnostic_format = protocol.split("\\\"diagnostics\\\"", maxsplit=1)[1].split(
        "secure_zero(&diagnostics", maxsplit=1
    )[0]

    assert "aiqa_management_protocol_handle_public_request_with_ports" in usb
    assert "copy_public_diagnostics" in usb
    assert 'strcmp(method, "system.diagnostics") == 0' in protocol
    for forbidden in ("transcript", "answer", "ssid", "password", "apiKey", "secret"):
        assert forbidden not in diagnostic_format


def test_startup_i2c_check_probes_only_known_board_devices():
    source = BOARD.read_text(encoding="utf-8")
    scan = source.split("esp_err_t board_wave_175c_i2c_scan", maxsplit=1)[1].split(
        "esp_err_t board_wave_175c_i2c_bus_handle", maxsplit=1
    )[0]

    assert "board_wave_175c_expected_i2c_devices" in scan
    assert "board_wave_175c_expected_i2c_device_count" in scan
    assert "address <= 0x77" not in scan


def test_chat_and_tts_log_sizes_status_and_elapsed_without_payload_text():
    chat = CHAT.read_text(encoding="utf-8")
    tts = TTS.read_text(encoding="utf-8")

    assert "AIQA_DIAG chat_request" in chat
    assert "request_bytes=%u" in chat
    assert "AIQA_DIAG chat_response" in chat
    assert "response_bytes=%u" in chat
    assert "body_overflow=%d" in chat
    assert "answer_overflow=%d" in chat
    assert "elapsed_ms=%u" in chat
    assert "AIQA_DIAG tts_request" in tts
    assert "text_bytes=%u" in tts
    assert "AIQA_DIAG tts_response" in tts
    assert "audio_bytes=%u" in tts
    assert "audio_chunks=%u" in tts
    assert "elapsed_ms=%u" in tts

    for source in (chat, tts):
        diagnostic_lines = "\n".join(
            line for line in source.splitlines() if "AIQA_DIAG" in line
        )
        assert "api_key" not in diagnostic_lines
        assert "request_body" not in diagnostic_lines
        assert "result->text" not in diagnostic_lines

    assert chat.rindex("AIQA_DIAG chat_response") > chat.index(
        "process_stream_remainder(&response)"
    )


def test_streaming_chat_answer_capacity_is_not_silently_truncated():
    source = CHAT.read_text(encoding="utf-8")
    append = source.split("static void append_result_delta", maxsplit=1)[1].split(
        "static void process_stream_event", maxsplit=1
    )[0]

    assert "answer_overflow" in append
    assert "copy_len < delta_len" in append
    stream_completion = source.split("process_stream_remainder(&response)", maxsplit=1)[1]
    assert "response.answer_overflow" in stream_completion
    assert "AIQA_CHAT_ERR_BUFFER_TOO_SMALL" in stream_completion
