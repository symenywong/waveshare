import subprocess
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/app_runtime/src/aiqa_runtime.c"


def _function_region(source: str, name: str, next_name: str) -> str:
    start = source.index(name)
    end = source.index(next_name, start + len(name))
    return source[start:end]


def test_asr_failure_event_preserves_interaction_generation():
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        (tmp_path / "esp_err.h").write_text(
            textwrap.dedent(
                """
                #pragma once
                typedef int esp_err_t;
                #define ESP_OK 0
                #define ESP_ERR_TIMEOUT 0x107
                """
            ),
            encoding="utf-8",
        )
        test_source = tmp_path / "asr_failure_event_test.c"
        test_source.write_text(
            textwrap.dedent(
                """
                #include "aiqa_runtime_events.h"
                #include <assert.h>
                #include <stdint.h>

                int main(void) {
                    const uint32_t generation = 0xA5A5u;
                    const aiqa_event_t event =
                        aiqa_runtime_asr_failure_event(generation);
                    assert(event.type == AIQA_EVENT_ASR_FAILED);
                    assert(event.error == AIQA_ERROR_ASR_FAILED);
                    assert(event.value == generation);
                    return 0;
                }
                """
            ),
            encoding="utf-8",
        )
        binary = tmp_path / "asr_failure_event_test"
        include_dirs = [
            tmp_path,
            ROOT / "components/app_runtime/include",
            ROOT / "components/app_core/include",
            ROOT / "components/asr_client/include",
            ROOT / "components/chat_client/include",
            ROOT / "components/config_store/include",
            ROOT / "components/provider_common/include",
        ]
        command = [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            *[f"-I{include_dir}" for include_dir in include_dirs],
            str(test_source),
            str(ROOT / "components/app_runtime/src/aiqa_runtime_events.c"),
            "-o",
            str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


def test_ui_state_delivery_uses_a_latest_state_mailbox_and_pairing_queue_set():
    source = RUNTIME.read_text(encoding="utf-8")
    post_state = _function_region(
        source,
        "static void post_ui_state",
        "static bool post_pairing_ui_command",
    )
    ui_task = _function_region(source, "static void ui_task", "static void audio_task")

    assert "#define AIQA_UI_STATE_QUEUE_DEPTH 1" in source
    assert "s_ui_state_queue" in source
    assert "xQueueOverwrite(s_ui_state_queue, &message)" in post_state
    assert "xQueueSend(s_ui_queue, &message, 0)" not in post_state
    assert "xQueueCreateSet" in source
    assert "xQueueAddToSet(s_ui_queue" in source
    assert "xQueueAddToSet(s_ui_state_queue" in source
    assert "xQueueSelectFromSet" in ui_task
    assert "activated == s_ui_queue" in ui_task
    assert "activated == s_ui_state_queue" in ui_task
    assert "xQueueReceive(s_ui_queue, &message, 0)" in ui_task
    assert "xQueueReceive(s_ui_state_queue, &message, 0)" in ui_task


def test_chat_token_ui_updates_are_throttled_before_entering_app_queue():
    source = RUNTIME.read_text(encoding="utf-8")
    callback = _function_region(
        source,
        "static void chat_stream_delta_callback",
        "static esp_err_t tts_playback_status_load",
    )

    assert "#define AIQA_CHAT_UI_UPDATE_INTERVAL_MS 150u" in source
    assert "last_ui_update_us" in source
    assert "AIQA_CHAT_UI_UPDATE_INTERVAL_MS" in callback
    assert "esp_timer_get_time()" in callback
    assert callback.index("s_latest_dialogue = updated;") < callback.index(
        "AIQA_EVENT_CHAT_TOKEN"
    )


def test_local_asr_failures_use_generation_scoped_events():
    source = RUNTIME.read_text(encoding="utf-8")
    recording_transition = _function_region(
        source,
        "static void handle_recording_transition",
        "static void handle_chat_prompt_transition",
    )
    audio_task = _function_region(source, "static void audio_task", "static void button_task")
    asr_task = _function_region(source, "static void asr_task", "static void chat_task")

    assert "aiqa_runtime_asr_failure_event" in recording_transition
    assert audio_task.count("aiqa_runtime_asr_failure_event(command.generation)") >= 4
    assert ".value = (uint32_t)ret" not in audio_task
    malformed_command = asr_task.split(
        "if ((command.type == AIQA_ASR_COMMAND_STATIC_SAMPLE", maxsplit=1
    )[1].split("if (!interaction_generation_is_current", maxsplit=1)[0]
    assert "aiqa_runtime_asr_failure_event(command.generation)" in malformed_command


def test_identity_preferences_fail_closed_and_use_separate_system_context():
    source = RUNTIME.read_text(encoding="utf-8")
    language_handler = _function_region(
        source,
        "static bool handle_language_switch_transcript",
        "static const char *weekday_zh",
    )
    local_handler = _function_region(
        source,
        "static bool handle_local_command_transcript",
        "esp_err_t aiqa_runtime_post_event",
    )
    chat_task = _function_region(source, "static void chat_task", "esp_err_t aiqa_runtime_start")

    assert "aiqa_config_save_dialogue_language" in language_handler
    language_before_publish = language_handler.split(
        "update_snapshot_dialogue_language", maxsplit=1
    )[0]
    language_after_save = language_before_publish.split(
        "aiqa_config_save_dialogue_language", maxsplit=1
    )[1]
    assert "if (save_ret != ESP_OK)" in language_after_save
    assert 'set_local_reply("语言设置保存失败，请重试。")' in language_after_save
    assert "return true;" in language_after_save
    assert "(void)aiqa_config_save_dialogue_language" not in language_handler
    assert "(void)aiqa_config_save_assistant_profile" not in local_handler

    name_branch = local_handler.split(
        "if (command.type == AIQA_LOCAL_COMMAND_SET_NAME)", maxsplit=1
    )[1].split("if (command.type == AIQA_LOCAL_COMMAND_SET_GENDER)", maxsplit=1)[0]
    gender_branch = local_handler.split(
        "if (command.type == AIQA_LOCAL_COMMAND_SET_GENDER)", maxsplit=1
    )[1]
    for branch, failure_reply in (
        (name_branch, 'set_local_reply("名字保存失败，请重试。")'),
        (gender_branch, 'set_local_reply("性别保存失败，请重试。")'),
    ):
        before_publish = branch.split("update_snapshot_profile", maxsplit=1)[0]
        after_save = before_publish.split(
            "aiqa_config_save_assistant_profile", maxsplit=1
        )[1]
        assert "if (save_ret != ESP_OK)" in after_save
        assert failure_reply in after_save
        assert "return true;" in after_save

    assert "aiqa_chat_send_streaming_with_contexts" in chat_task
    assert "aiqa_chat_send_once_with_contexts" in chat_task
    assert "config_snapshot.user_prefs.dialogue_language" in chat_task
    for call_name in (
        "aiqa_chat_send_streaming_with_contexts(",
        "aiqa_chat_send_once_with_contexts(",
    ):
        call = chat_task.split(call_name, maxsplit=1)[1].split(");", maxsplit=1)[0]
        conversation_argument = "has_conversation_context ? conversation_memory : NULL"
        profile_argument = "profile_context[0] != '\\0' ? profile_context : NULL"
        assert conversation_argument in call
        assert profile_argument in call
        assert call.index(conversation_argument) < call.index(profile_argument)
