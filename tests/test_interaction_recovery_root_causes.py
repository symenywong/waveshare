import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/app_runtime/src/aiqa_runtime.c"
ASR_CLIENT = ROOT / "components/asr_client/src/aiqa_asr_client.c"
CHAT_CLIENT = ROOT / "components/chat_client/src/aiqa_chat_client.c"
TTS_CLIENT = ROOT / "components/tts_client/src/aiqa_tts_client.c"
DISPLAY = ROOT / "components/board_wave_175c/src/board_wave_175c_display.c"
CAPTURE_HW = ROOT / "components/audio_capture/src/aiqa_audio_capture_hw.c"


def _function_region(source: str, name: str, next_name: str) -> str:
    start = source.index(name)
    end = source.index(next_name, start + len(name))
    return source[start:end]


def _compile_and_run(source: str, sources: list[Path], include_dirs: list[Path]) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        test_source = tmp_path / "test.c"
        binary = tmp_path / "test"
        test_source.write_text(textwrap.dedent(source), encoding="utf-8")
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                *[f"-I{path}" for path in include_dirs],
                str(test_source),
                *[str(path) for path in sources],
                "-o",
                str(binary),
            ],
            cwd=ROOT,
            check=True,
        )
        subprocess.run([str(binary)], cwd=ROOT, check=True)


def test_request_epoch_closes_cancel_before_registration_window():
    _compile_and_run(
        """
        #include "aiqa_request_epoch.h"
        #include <assert.h>

        int main(void) {
            aiqa_request_epoch_t epoch = AIQA_REQUEST_EPOCH_INITIALIZER;
            const uint32_t request_a = aiqa_request_epoch_capture(&epoch);
            assert(aiqa_request_epoch_is_current(&epoch, request_a));

            aiqa_request_epoch_cancel(&epoch);
            assert(!aiqa_request_epoch_is_current(&epoch, request_a));

            const uint32_t request_b = aiqa_request_epoch_capture(&epoch);
            assert(aiqa_request_epoch_is_current(&epoch, request_b));
            assert(request_b != request_a);
            return 0;
        }
        """,
        [ROOT / "components/provider_common/src/aiqa_request_epoch.c"],
        [ROOT / "components/provider_common/include"],
    )


def test_asr_phase_deadline_is_wrap_safe_and_state_scoped():
    _compile_and_run(
        """
        #include "aiqa_state_machine.h"
        #include <assert.h>
        #include <stdint.h>

        int main(void) {
            assert(!aiqa_state_machine_asr_deadline_expired(
                AIQA_STATE_TRANSCRIBING, 1000u, 1000u + AIQA_ASR_PHASE_TIMEOUT_MS - 1u));
            assert(aiqa_state_machine_asr_deadline_expired(
                AIQA_STATE_TRANSCRIBING, 1000u, 1000u + AIQA_ASR_PHASE_TIMEOUT_MS));
            assert(aiqa_state_machine_asr_deadline_expired(
                AIQA_STATE_ASR_JOB_PENDING, UINT32_MAX - 10u,
                (uint32_t)(UINT32_MAX - 10u + AIQA_ASR_PHASE_TIMEOUT_MS)));
            assert(!aiqa_state_machine_asr_deadline_expired(
                AIQA_STATE_RECORDING, 0u, UINT32_MAX));
            assert(!aiqa_state_machine_asr_deadline_expired(
                AIQA_STATE_THINKING, 0u, UINT32_MAX));
            return 0;
        }
        """,
        [ROOT / "components/app_core/src/aiqa_state_machine.c"],
        [ROOT / "components/app_core/include"],
    )


def test_network_cancellation_is_token_based_and_never_reconnects_on_state_task():
    for path in (ASR_CLIENT, CHAT_CLIENT, TTS_CLIENT):
        source = path.read_text(encoding="utf-8")
        cancel = _function_region(
            source,
            "void aiqa_" + path.parent.parent.name.replace("_client", "") + "_cancel_active_request",
            "static void",
        )
        assert "aiqa_request_epoch_cancel" in cancel
        assert "esp_http_client_cancel_request" not in cancel
        assert "xSemaphoreTake" not in cancel


def test_asr_transport_checks_epoch_through_open_upload_and_response():
    source = ASR_CLIENT.read_text(encoding="utf-8")
    runtime = RUNTIME.read_text(encoding="utf-8")
    pcm = source.split("esp_err_t aiqa_asr_transcribe_pcm_wav_once_with_epoch", maxsplit=1)[1]

    assert "uint32_t request_epoch;" in runtime
    assert ".request_epoch = aiqa_asr_request_epoch_capture()" in runtime
    assert "aiqa_asr_transcribe_pcm_wav_once_with_epoch" in runtime
    assert "command.request_epoch" in runtime
    assert "aiqa_request_epoch_capture" not in pcm.split(
        "esp_err_t aiqa_asr_transcribe_pcm_wav_once", maxsplit=1
    )[0]
    assert pcm.count("aiqa_request_epoch_is_current") >= 4
    assert "AIQA_ASR_SOCKET_TIMEOUT_MS" in source
    assert "esp_http_client_cancel_request" not in source


def test_chat_and_tts_streams_bind_epoch_before_worker_and_poll_cancellation():
    runtime = RUNTIME.read_text(encoding="utf-8")
    chat = CHAT_CLIENT.read_text(encoding="utf-8")
    tts = TTS_CLIENT.read_text(encoding="utf-8")

    assert ".request_epoch = aiqa_chat_request_epoch_capture()" in runtime
    assert "aiqa_chat_send_streaming_with_contexts_epoch" in runtime
    assert "command.request_epoch" in runtime
    assert "aiqa_tts_speak_streaming_with_epoch" in runtime
    assert "aiqa_tts_request_epoch_capture()" in runtime
    for source in (chat, tts):
        assert "esp_http_client_perform(client)" not in source
        assert "esp_http_client_open" in source
        assert "esp_http_client_read" in source
        assert source.count("aiqa_request_epoch_is_current") >= 3


def test_runtime_has_asr_watchdog_and_does_not_swallow_new_audio_start():
    source = RUNTIME.read_text(encoding="utf-8")
    app_task = _function_region(source, "static void app_state_task", "static void ui_task")
    audio_task = _function_region(source, "static void audio_task", "static void button_task")
    recording_loop = audio_task.split("while (recording)", maxsplit=1)[1].split(
        "aiqa_audio_command_t pending", maxsplit=1
    )[0]

    assert "xQueueReceive(s_app_queue, &event, portMAX_DELAY)" not in app_task
    assert "AIQA_APP_EVENT_POLL_MS" in app_task
    assert "aiqa_state_machine_asr_deadline_expired" in app_task
    assert "AIQA_EVENT_TIMEOUT" in app_task
    assert "const bool event_received" in app_task
    assert "watchdog_may_replace_event" in app_task
    for protected_event in (
        "AIQA_EVENT_PRESS_START",
        "AIQA_EVENT_CANCEL",
        "AIQA_EVENT_FACTORY_RESET",
        "AIQA_EVENT_ASR_DONE",
        "AIQA_EVENT_ASR_FAILED",
    ):
        assert protected_event not in app_task.split(
            "const bool watchdog_may_replace_event", maxsplit=1
        )[1].split(";", maxsplit=1)[0]
    assert app_task.index("aiqa_state_machine_asr_deadline_expired") > app_task.index(
        "xQueueReceive"
    )
    assert "interaction_generation_is_current(command.generation)" in recording_loop


def test_terminal_events_are_reliable_and_result_commits_are_generation_atomic():
    source = RUNTIME.read_text(encoding="utf-8")
    audio_task = _function_region(source, "static void audio_task", "static void button_task")
    asr_task = _function_region(source, "static void asr_task", "static void chat_stream_delta_callback")
    chat_task = _function_region(source, "static void chat_task", "esp_err_t aiqa_runtime_start")
    button_task = _function_region(source, "static void button_task", "static aiqa_management_result_t")

    assert "xQueueSend(s_app_queue, &event, portMAX_DELAY)" in source
    assert "post_critical_app_event(event)" in asr_task
    assert "post_critical_app_event(event)" in chat_task
    assert "post_critical_app_event(event)" in button_task
    assert audio_task.count("post_critical_app_event") >= 4
    assert asr_task.count("post_critical_app_event") >= 5
    assert asr_task.index("lock_current_interaction(command.generation)") < asr_task.index(
        "s_latest_transcript"
    )
    assert asr_task.index("s_latest_transcript") < asr_task.index(
        "unlock_current_interaction()"
    )
    assert chat_task.index("lock_current_interaction(command.generation)") < chat_task.index(
        "aiqa_dialogue_view_set_pet(&s_latest_dialogue, result.text)"
    )


def test_ui_retries_failed_full_pages_instead_of_animating_stale_page():
    source = RUNTIME.read_text(encoding="utf-8")
    ui_task = _function_region(source, "static void ui_task", "static void audio_task")

    assert "page_dirty" in ui_task
    assert "page_dirty = display_ret != ESP_OK" in ui_task
    assert "received || page_dirty" in ui_task


def test_display_uses_persistent_buffers_so_flush_timeout_cannot_free_inflight_dma():
    source = DISPLAY.read_text(encoding="utf-8")
    solid = _function_region(source, "static esp_err_t display_draw_solid_rect", "static esp_err_t display_draw_text_line")
    text_line = _function_region(source, "static esp_err_t display_draw_text_line", "static esp_err_t display_draw_centered_text_line")
    pet = _function_region(source, "static esp_err_t display_draw_pet_expression", "esp_err_t board_wave_175c_display_init")

    assert "s_lcd_dma_buffer" in source
    assert "s_pet_cells" in source
    assert "s_lcd_flush_pending" in source
    assert "LCD_FLUSH_MAX_CONSECUTIVE_TIMEOUTS" in source
    assert "esp_restart()" in source
    assert "_Static_assert" in source
    for function in (solid, text_line, pet):
        assert "heap_caps_malloc" not in function
        assert "heap_caps_calloc" not in function
        assert "free(" not in function


def test_pairing_failures_cannot_block_normal_page_retry():
    source = RUNTIME.read_text(encoding="utf-8")
    ui_task = _function_region(source, "static void ui_task", "static void audio_task")

    clear_branch = ui_task.split("AIQA_UI_MESSAGE_PAIRING_CLEAR", maxsplit=1)[1]
    assert "pairing_overlay = false" in clear_branch
    assert "page_dirty = have_last_message && !cleared" in clear_branch
    show_branch = ui_task.split("AIQA_UI_MESSAGE_PAIRING_SHOW", maxsplit=1)[1].split(
        "AIQA_UI_MESSAGE_PAIRING_CLEAR", maxsplit=1
    )[0]
    assert "page_dirty = have_last_message" in show_branch


def test_capture_codec_state_changes_only_after_successful_open_and_close():
    source = CAPTURE_HW.read_text(encoding="utf-8")
    start = _function_region(
        source,
        "esp_err_t aiqa_audio_capture_hw_start",
        "esp_err_t aiqa_audio_capture_hw_read_mono",
    )
    stop = _function_region(
        source,
        "esp_err_t aiqa_audio_capture_hw_stop",
        "bool aiqa_audio_capture_hw_is_ready",
    )

    assert "esp_codec_dev_close(s_codec)" in start
    assert start.index("esp_codec_dev_set_in_gain") < start.index(
        "esp_codec_dev_close(s_codec)"
    )
    assert stop.index("esp_codec_dev_close(s_codec)") < stop.index(
        "s_started = false"
    )
