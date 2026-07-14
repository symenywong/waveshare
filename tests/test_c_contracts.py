import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class CContractTests(unittest.TestCase):
    def compile_and_run(self, source: str, extra_sources: list[str], include_dirs: list[str]) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            test_c = Path(tmp) / "contract_test.c"
            esp_err_h = Path(tmp) / "esp_err.h"
            binary = Path(tmp) / "contract_test"
            test_c.write_text(source, encoding="utf-8")
            esp_err_h.write_text(
                textwrap.dedent(
                    """
                    #pragma once
                    typedef int esp_err_t;
                    #define ESP_OK 0
                    #define ESP_FAIL -1
                    #define ESP_ERR_NO_MEM 0x101
                    #define ESP_ERR_INVALID_ARG 0x102
                    #define ESP_ERR_INVALID_STATE 0x103
                    #define ESP_ERR_NOT_FOUND 0x105
                    #define ESP_ERR_TIMEOUT 0x107
                    const char *esp_err_to_name(esp_err_t code);
                    """
                ),
                encoding="utf-8",
            )
            cmd = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                f"-I{tmp}",
                *[f"-I{REPO_ROOT / include_dir}" for include_dir in include_dirs],
                str(test_c),
                *[str(REPO_ROOT / source_file) for source_file in extra_sources],
                "-o",
                str(binary),
            ]
            subprocess.run(cmd, cwd=REPO_ROOT, check=True)
            subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)

    def test_state_machine_push_to_talk_happy_path(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                static void dispatch(aiqa_state_machine_t *m, aiqa_event_type_t type, aiqa_state_t expected) {
                    aiqa_event_t event = {.type = type, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t t = aiqa_state_machine_dispatch(m, event);
                    assert(t.accepted);
                    assert(m->state == expected);
                }

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    assert(machine.state == AIQA_STATE_BOOT);
                    dispatch(&machine, AIQA_EVENT_BOOTED, AIQA_STATE_CONFIG_CHECK);
                    dispatch(&machine, AIQA_EVENT_CONFIG_READY, AIQA_STATE_NETWORK_CONNECTING);
                    dispatch(&machine, AIQA_EVENT_NETWORK_READY, AIQA_STATE_IDLE);
                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    dispatch(&machine, AIQA_EVENT_ASR_DONE, AIQA_STATE_THINKING);
                    dispatch(&machine, AIQA_EVENT_CHAT_DONE, AIQA_STATE_IDLE_WITH_RESULT);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_cancel_cannot_bypass_configuration_and_network(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);

                    aiqa_event_t cancel = {.type = AIQA_EVENT_CANCEL, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t ignored = aiqa_state_machine_dispatch(&machine, cancel);
                    assert(!ignored.accepted);
                    assert(machine.state == AIQA_STATE_BOOT);

                    aiqa_event_t booted = {.type = AIQA_EVENT_BOOTED, .error = AIQA_ERROR_NONE, .value = 0};
                    (void)aiqa_state_machine_dispatch(&machine, booted);
                    ignored = aiqa_state_machine_dispatch(&machine, cancel);
                    assert(!ignored.accepted);
                    assert(machine.state == AIQA_STATE_CONFIG_CHECK);

                    aiqa_event_t ready = {.type = AIQA_EVENT_CONFIG_READY, .error = AIQA_ERROR_NONE, .value = 0};
                    (void)aiqa_state_machine_dispatch(&machine, ready);
                    ignored = aiqa_state_machine_dispatch(&machine, cancel);
                    assert(!ignored.accepted);
                    assert(machine.state == AIQA_STATE_NETWORK_CONNECTING);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_chat_started_can_begin_text_interaction_after_network_ready(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                static void dispatch(aiqa_state_machine_t *m, aiqa_event_type_t type, aiqa_state_t expected) {
                    aiqa_event_t event = {.type = type, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t t = aiqa_state_machine_dispatch(m, event);
                    assert(t.accepted);
                    assert(m->state == expected);
                }

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    dispatch(&machine, AIQA_EVENT_BOOTED, AIQA_STATE_CONFIG_CHECK);
                    dispatch(&machine, AIQA_EVENT_CONFIG_READY, AIQA_STATE_NETWORK_CONNECTING);
                    dispatch(&machine, AIQA_EVENT_NETWORK_READY, AIQA_STATE_IDLE);
                    dispatch(&machine, AIQA_EVENT_CHAT_STARTED, AIQA_STATE_THINKING);
                    dispatch(&machine, AIQA_EVENT_CHAT_TOKEN, AIQA_STATE_THINKING);
                    dispatch(&machine, AIQA_EVENT_CHAT_DONE, AIQA_STATE_IDLE_WITH_RESULT);
                    dispatch(&machine, AIQA_EVENT_CHAT_STARTED, AIQA_STATE_THINKING);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_asr_done_can_drive_chat_started_without_race_warning(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                static void dispatch(aiqa_state_machine_t *m, aiqa_event_type_t type, aiqa_state_t expected) {
                    aiqa_event_t event = {.type = type, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t t = aiqa_state_machine_dispatch(m, event);
                    assert(t.accepted);
                    assert(m->state == expected);
                }

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    dispatch(&machine, AIQA_EVENT_BOOTED, AIQA_STATE_CONFIG_CHECK);
                    dispatch(&machine, AIQA_EVENT_CONFIG_READY, AIQA_STATE_NETWORK_CONNECTING);
                    dispatch(&machine, AIQA_EVENT_NETWORK_READY, AIQA_STATE_IDLE);
                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    dispatch(&machine, AIQA_EVENT_ASR_STARTED, AIQA_STATE_TRANSCRIBING);
                    dispatch(&machine, AIQA_EVENT_ASR_DONE, AIQA_STATE_THINKING);
                    dispatch(&machine, AIQA_EVENT_CHAT_STARTED, AIQA_STATE_THINKING);
                    dispatch(&machine, AIQA_EVENT_CHAT_DONE, AIQA_STATE_IDLE_WITH_RESULT);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_long_press_interrupts_waiting_interaction_for_new_recording(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                static void dispatch(aiqa_state_machine_t *m, aiqa_event_type_t type, aiqa_state_t expected) {
                    aiqa_event_t event = {.type = type, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t t = aiqa_state_machine_dispatch(m, event);
                    assert(t.accepted);
                    assert(m->state == expected);
                }

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    dispatch(&machine, AIQA_EVENT_BOOTED, AIQA_STATE_CONFIG_CHECK);
                    dispatch(&machine, AIQA_EVENT_CONFIG_READY, AIQA_STATE_NETWORK_CONNECTING);
                    dispatch(&machine, AIQA_EVENT_NETWORK_READY, AIQA_STATE_IDLE);

                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    dispatch(&machine, AIQA_EVENT_ASR_DONE, AIQA_STATE_THINKING);

                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    assert(machine.last_error == AIQA_ERROR_NONE);

                    aiqa_event_t stale_done = {.type = AIQA_EVENT_CHAT_DONE, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t ignored = aiqa_state_machine_dispatch(&machine, stale_done);
                    assert(!ignored.accepted);
                    assert(machine.state == AIQA_STATE_RECORDING);

                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_long_press_interrupts_transcribing_or_pending_asr(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                static void dispatch(aiqa_state_machine_t *m, aiqa_event_type_t type, aiqa_state_t expected) {
                    aiqa_event_t event = {.type = type, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t t = aiqa_state_machine_dispatch(m, event);
                    assert(t.accepted);
                    assert(m->state == expected);
                }

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    dispatch(&machine, AIQA_EVENT_BOOTED, AIQA_STATE_CONFIG_CHECK);
                    dispatch(&machine, AIQA_EVENT_CONFIG_READY, AIQA_STATE_NETWORK_CONNECTING);
                    dispatch(&machine, AIQA_EVENT_NETWORK_READY, AIQA_STATE_IDLE);

                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    dispatch(&machine, AIQA_EVENT_ASR_JOB_SUBMITTED, AIQA_STATE_ASR_JOB_PENDING);
                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_recoverable_asr_error_can_retry_with_next_long_press(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                static void dispatch(aiqa_state_machine_t *m, aiqa_event_type_t type, aiqa_state_t expected) {
                    aiqa_event_t event = {.type = type, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t t = aiqa_state_machine_dispatch(m, event);
                    assert(t.accepted);
                    assert(m->state == expected);
                }

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    dispatch(&machine, AIQA_EVENT_BOOTED, AIQA_STATE_CONFIG_CHECK);
                    dispatch(&machine, AIQA_EVENT_CONFIG_READY, AIQA_STATE_NETWORK_CONNECTING);
                    dispatch(&machine, AIQA_EVENT_NETWORK_READY, AIQA_STATE_IDLE);
                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    dispatch(&machine, AIQA_EVENT_ASR_FAILED, AIQA_STATE_ERROR);
                    assert(machine.last_error == AIQA_ERROR_ASR_FAILED);

                    dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                    assert(machine.last_error == AIQA_ERROR_NONE);
                    dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_hard_configuration_error_cannot_retry_with_long_press(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    aiqa_event_t event = {.type = AIQA_EVENT_BOOTED, .error = AIQA_ERROR_NONE, .value = 0};
                    (void)aiqa_state_machine_dispatch(&machine, event);

                    event.type = AIQA_EVENT_CONFIG_MISSING;
                    aiqa_transition_t transition = aiqa_state_machine_dispatch(&machine, event);
                    assert(transition.accepted);
                    assert(machine.state == AIQA_STATE_ERROR);
                    assert(machine.last_error == AIQA_ERROR_CONFIG_MISSING);

                    event.type = AIQA_EVENT_PRESS_START;
                    transition = aiqa_state_machine_dispatch(&machine, event);
                    assert(!transition.accepted);
                    assert(machine.state == AIQA_STATE_ERROR);
                    assert(machine.last_error == AIQA_ERROR_CONFIG_MISSING);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_state_machine_soaks_repeated_ptt_chat_cycles(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_state_machine.h"
                #include <assert.h>

                static void dispatch(aiqa_state_machine_t *m, aiqa_event_type_t type, aiqa_state_t expected) {
                    aiqa_event_t event = {.type = type, .error = AIQA_ERROR_NONE, .value = 0};
                    aiqa_transition_t t = aiqa_state_machine_dispatch(m, event);
                    assert(t.accepted);
                    assert(m->state == expected);
                }

                int main(void) {
                    aiqa_state_machine_t machine;
                    aiqa_state_machine_init(&machine);
                    dispatch(&machine, AIQA_EVENT_BOOTED, AIQA_STATE_CONFIG_CHECK);
                    dispatch(&machine, AIQA_EVENT_CONFIG_READY, AIQA_STATE_NETWORK_CONNECTING);
                    dispatch(&machine, AIQA_EVENT_NETWORK_READY, AIQA_STATE_IDLE);

                    for (int cycle = 0; cycle < 200; ++cycle) {
                        dispatch(&machine, AIQA_EVENT_PRESS_START, AIQA_STATE_RECORDING);
                        dispatch(&machine, AIQA_EVENT_PRESS_END, AIQA_STATE_TRANSCRIBING);
                        dispatch(&machine, AIQA_EVENT_ASR_STARTED, AIQA_STATE_TRANSCRIBING);
                        dispatch(&machine, AIQA_EVENT_ASR_DONE, AIQA_STATE_THINKING);
                        dispatch(&machine, AIQA_EVENT_CHAT_STARTED, AIQA_STATE_THINKING);
                        dispatch(&machine, AIQA_EVENT_CHAT_DONE, AIQA_STATE_IDLE_WITH_RESULT);
                    }
                    assert(machine.transition_count >= 800);
                    assert(machine.state == AIQA_STATE_IDLE_WITH_RESULT);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_state_machine.c"],
            ["components/app_core/include"],
        )

    def test_release_hardening_policy_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_hardening.h"
                #include <assert.h>

                int main(void) {
                    aiqa_hardening_policy_t policy = aiqa_hardening_default_policy();
                    assert(aiqa_hardening_policy_is_safe(&policy));
                    assert(policy.redact_transcripts);
                    assert(policy.redact_answers);

                    assert(!aiqa_hardening_heap_allows_model_request(&policy, policy.min_model_heap_bytes - 1));
                    assert(aiqa_hardening_heap_allows_model_request(&policy, policy.min_model_heap_bytes));

                    assert(aiqa_hardening_rate_limit_until_ms(&policy, 1000, 0) ==
                           1000 + policy.rate_limit_cooldown_ms);
                    assert(aiqa_hardening_rate_limit_until_ms(&policy, 1000, 30000) == 31000);
                    assert(aiqa_hardening_request_in_cooldown(1500, 31000));
                    assert(!aiqa_hardening_request_in_cooldown(31000, 31000));

                    policy.max_consecutive_provider_failures = 0;
                    assert(!aiqa_hardening_policy_is_safe(&policy));
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_hardening.c"],
            ["components/app_core/include"],
        )

    def test_tts_pcm_buffer_collects_audio_before_playback(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_tts_pcm_buffer.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    uint8_t storage[6] = {0};
                    aiqa_tts_pcm_buffer_t buffer;
                    aiqa_tts_pcm_buffer_init(&buffer, storage, sizeof(storage));
                    assert(buffer.length == 0);
                    assert(buffer.capacity == sizeof(storage));
                    assert(!buffer.overflow);

                    const uint8_t first[] = {1, 2, 3};
                    const uint8_t second[] = {4, 5, 6};
                    assert(aiqa_tts_pcm_buffer_append(&buffer, first, sizeof(first)) ==
                           AIQA_TTS_PCM_BUFFER_OK);
                    assert(aiqa_tts_pcm_buffer_append(&buffer, second, sizeof(second)) ==
                           AIQA_TTS_PCM_BUFFER_OK);
                    assert(buffer.length == sizeof(storage));
                    assert(memcmp(storage, "\\x01\\x02\\x03\\x04\\x05\\x06", sizeof(storage)) == 0);

                    const uint8_t extra[] = {7, 8};
                    assert(aiqa_tts_pcm_buffer_append(&buffer, extra, sizeof(extra)) ==
                           AIQA_TTS_PCM_BUFFER_FULL);
                    assert(buffer.length == sizeof(storage));
                    assert(buffer.overflow);
                    assert(aiqa_tts_pcm_buffer_append(0, first, sizeof(first)) ==
                           AIQA_TTS_PCM_BUFFER_INVALID_ARG);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_tts_pcm_buffer.c"],
            ["components/app_core/include"],
        )

    def test_dialogue_view_contract_keeps_round_screen_lines_short(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_dialogue_view.h"
                #include "board_wave_175c_display_contract.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_dialogue_view_t view;
                    aiqa_dialogue_view_clear(&view);
                    assert(view.pet_emotion == AIQA_DIALOGUE_EMOTION_NONE);
                    aiqa_dialogue_view_set_user(&view, "hello tiny pet, please explain rain");
                    aiqa_dialogue_view_set_pet(&view, "Rain is cloud water falling back down.");
                    assert(view.has_dialogue);
                    assert(strcmp(view.user_line, "IN HELLO TINY PET") == 0);
                    assert(strcmp(view.pet_line, "OUT RAIN IS CLOUD WATER") == 0);
                    assert(view.pet_emotion == AIQA_DIALOGUE_EMOTION_NONE);

                    board_wave_175c_display_rect_t rect = {0};
                    assert(board_wave_175c_display_centered_text_rect(strlen(view.user_line), 2, 372, &rect));
                    assert(board_wave_175c_display_centered_text_rect(strlen(view.pet_line), 2, 334, &rect));

                    aiqa_dialogue_view_set_user(&view, "今天天气不错");
                    aiqa_dialogue_view_set_pet(&view, "好的，我听到了");
                    assert(strcmp(view.user_line, "IN VOICE") == 0);
                    assert(strcmp(view.pet_line, "OUT READY") == 0);
                    assert(view.pet_emotion == AIQA_DIALOGUE_EMOTION_NONE);

                    aiqa_dialogue_view_set_pet(&view, "我现在很开心");
                    assert(strcmp(view.pet_line, "OUT READY") == 0);
                    assert(view.pet_emotion == AIQA_DIALOGUE_EMOTION_HAPPY);
                    return 0;
                }
                """
            ),
            [
                "components/app_core/src/aiqa_dialogue_view.c",
                "components/board_wave_175c/src/board_wave_175c_display_contract.c",
            ],
            [
                "components/app_core/include",
                "components/board_wave_175c/include",
            ],
        )

    def test_conversation_memory_keeps_recent_turns_for_chat_context(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_conversation_memory.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_conversation_memory_t memory;
                    aiqa_conversation_memory_init(&memory);

                    char context[AIQA_CONVERSATION_MEMORY_CONTEXT_MAX_LEN] = {0};
                    assert(!aiqa_conversation_memory_build_context(&memory, context, sizeof(context)));
                    assert(context[0] == '\\0');

                    assert(aiqa_conversation_memory_add_turn(&memory, "first question", "first answer"));
                    assert(aiqa_conversation_memory_add_turn(&memory, "second question", "second answer"));
                    assert(aiqa_conversation_memory_add_turn(&memory, "third question", "third answer"));
                    assert(aiqa_conversation_memory_add_turn(&memory, "fourth question", "fourth answer"));

                    assert(memory.count == AIQA_CONVERSATION_MEMORY_MAX_TURNS);
                    assert(aiqa_conversation_memory_build_context(&memory, context, sizeof(context)));
                    assert(strstr(context, "first question") == 0);
                    assert(strstr(context, "second question") != 0);
                    assert(strstr(context, "third answer") != 0);
                    assert(strstr(context, "fourth question") != 0);
                    assert(strstr(context, "User: second question") < strstr(context, "User: fourth question"));

                    aiqa_conversation_memory_clear(&memory);
                    assert(!aiqa_conversation_memory_build_context(&memory, context, sizeof(context)));
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_conversation_memory.c"],
            ["components/app_core/include"],
        )

    def test_assistant_profile_defaults_updates_and_context(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_assistant_profile.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_assistant_profile_t profile = aiqa_assistant_profile_default();
                    assert(strcmp(profile.name, "AIQA") == 0);
                    assert(profile.gender == AIQA_ASSISTANT_GENDER_NEUTRAL);
                    assert(aiqa_assistant_profile_is_valid(&profile));

                    assert(aiqa_assistant_profile_set_name(&profile, "小智"));
                    assert(strcmp(profile.name, "小智") == 0);
                    assert(aiqa_assistant_profile_set_gender(&profile, AIQA_ASSISTANT_GENDER_FEMALE));

                    char context[160] = {0};
                    assert(aiqa_assistant_profile_build_context(&profile, context, sizeof(context)));
                    assert(strstr(context, "Assistant profile") != 0);
                    assert(strstr(context, "小智") != 0);
                    assert(strstr(context, "female") != 0);

                    assert(!aiqa_assistant_profile_set_name(&profile, ""));
                    assert(!aiqa_assistant_profile_set_name(&profile, "abcdefghijklmnopqrstuvwxyz0123456789"));
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_assistant_profile.c"],
            ["components/app_core/include"],
        )

    def test_local_command_parser_handles_volume_power_time_and_profile(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_local_command.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_local_command_t command;

                    assert(aiqa_local_command_parse("音量调大一点", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_VOLUME_RELATIVE);
                    assert(command.value == 10);

                    assert(aiqa_local_command_parse("音量调小一点", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_VOLUME_RELATIVE);
                    assert(command.value == -10);

                    assert(aiqa_local_command_parse("音量调到50", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_VOLUME_SET);
                    assert(command.value == 50);

                    assert(aiqa_local_command_parse("音量调到150", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_VOLUME_SET);
                    assert(command.value == 100);

                    assert(aiqa_local_command_parse("静音", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_VOLUME_SET);
                    assert(command.value == 0);

                    assert(aiqa_local_command_parse("当前音量是多少", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_VOLUME_QUERY);

                    assert(aiqa_local_command_parse("现在电量多少", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_BATTERY_QUERY);

                    assert(aiqa_local_command_parse("今天日期", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_DATE_QUERY);

                    assert(aiqa_local_command_parse("现在几点", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_TIME_QUERY);

                    assert(aiqa_local_command_parse("星期几", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_WEEKDAY_QUERY);

                    assert(aiqa_local_command_parse("你叫小智", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_SET_NAME);
                    assert(strcmp(command.text, "小智") == 0);

                    assert(aiqa_local_command_parse("把名字改成 小夏", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_SET_NAME);
                    assert(strcmp(command.text, "小夏") == 0);

                    assert(aiqa_local_command_parse("你是女声", &command));
                    assert(command.type == AIQA_LOCAL_COMMAND_SET_GENDER);
                    assert(command.gender == AIQA_ASSISTANT_GENDER_FEMALE);

                    assert(!aiqa_local_command_parse("讲个故事", &command));
                    return 0;
                }
                """
            ),
            [
                "components/app_core/src/aiqa_local_command.c",
                "components/app_core/src/aiqa_assistant_profile.c",
            ],
            ["components/app_core/include"],
        )

    def test_runtime_ui_prefers_dialogue_on_answer_page(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_runtime_ui.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_dialogue_view_t dialogue;
                    aiqa_dialogue_view_clear(&dialogue);
                    aiqa_dialogue_view_set_user(&dialogue, "hello pet");
                    aiqa_dialogue_view_set_pet(&dialogue, "happy to help");

                    board_wave_175c_display_page_t page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(strcmp(page.title, "PET") == 0);
                    assert(strcmp(page.status, "SPEAK") == 0);
                    assert(strcmp(page.detail, "OUT HAPPY TO HELP") == 0);
                    assert(strcmp(page.hint, "IN HELLO PET") == 0);
                    assert(!page.is_error);

                    page = aiqa_runtime_ui_page_for_dialogue(AIQA_STATE_THINKING, AIQA_ERROR_NONE, &dialogue);
                    assert(strcmp(page.status, "THINK") == 0);
                    assert(strcmp(page.detail, "OUT HAPPY TO HELP") == 0);
                    assert(strcmp(page.hint, "IN HELLO PET") == 0);

                    aiqa_dialogue_view_clear(&dialogue);
                    aiqa_dialogue_view_set_user(&dialogue, "what time is it");
                    page = aiqa_runtime_ui_page_for_dialogue(AIQA_STATE_THINKING, AIQA_ERROR_NONE, &dialogue);
                    assert(strcmp(page.status, "THINK") == 0);
                    assert(strcmp(page.detail, "OUT WAIT") == 0);
                    assert(strcmp(page.hint, "IN WHAT TIME IS IT") == 0);
                    return 0;
                }
                """
            ),
            [
                "components/app_runtime/src/aiqa_runtime_ui.c",
                "components/app_core/src/aiqa_dialogue_view.c",
            ],
            [
                "components/app_runtime/include",
                "components/app_core/include",
                "components/board_wave_175c/include",
            ],
        )

    def test_runtime_ui_selects_codex_pet_scenes_and_emotions(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_runtime_ui.h"
                #include "aiqa_dialogue_view.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    board_wave_175c_display_page_t page =
                        aiqa_runtime_ui_page_for(AIQA_STATE_IDLE, AIQA_ERROR_NONE);
                    assert(strcmp(page.status, "READY") == 0);
                    assert(strcmp(page.hint, "HOLD BOOT") == 0);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_IDLE);

                    page = aiqa_runtime_ui_page_for(AIQA_STATE_RECORDING, AIQA_ERROR_NONE);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_LISTENING);

                    page = aiqa_runtime_ui_page_for(AIQA_STATE_THINKING, AIQA_ERROR_NONE);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_THINKING);

                    page = aiqa_runtime_ui_page_for(AIQA_STATE_NETWORK_CONNECTING, AIQA_ERROR_NONE);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING);

                    aiqa_dialogue_view_t dialogue;
                    aiqa_dialogue_view_clear(&dialogue);
                    aiqa_dialogue_view_set_pet(&dialogue, "here is your answer");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING);

                    aiqa_dialogue_view_set_pet(&dialogue, "haha that was funny");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING);

                    aiqa_dialogue_view_set_pet(&dialogue, "I am shy");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_SHY);

                    aiqa_dialogue_view_set_pet(&dialogue, "今天很开心");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_HAPPY);

                    aiqa_dialogue_view_set_pet(&dialogue, "我有点难过");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_SAD);

                    aiqa_dialogue_view_set_pet(&dialogue, "突然害羞了");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_SHY);

                    aiqa_dialogue_view_set_pet(&dialogue, "有点沮丧");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED);

                    aiqa_dialogue_view_set_pet(&dialogue, "一起蹦跳吧");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING);

                    aiqa_dialogue_view_set_pet(&dialogue, "哈哈大笑");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING);

                    aiqa_dialogue_view_set_pet(&dialogue, "我要大哭了");
                    page = aiqa_runtime_ui_page_for_dialogue(
                        AIQA_STATE_IDLE_WITH_RESULT,
                        AIQA_ERROR_NONE,
                        &dialogue);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_CRYING);

                    page = aiqa_runtime_ui_page_for(AIQA_STATE_ERROR, AIQA_ERROR_ASR_FAILED);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_SAD);

                    page = aiqa_runtime_ui_page_for(AIQA_STATE_ERROR, AIQA_ERROR_TIMEOUT);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED);

                    page = aiqa_runtime_ui_page_for(AIQA_STATE_ERROR, AIQA_ERROR_CHAT_FAILED);
                    assert(page.expression == BOARD_WAVE_175C_PET_EXPRESSION_CRYING);
                    return 0;
                }
                """
            ),
            [
                "components/app_runtime/src/aiqa_runtime_ui.c",
                "components/app_core/src/aiqa_dialogue_view.c",
            ],
            [
                "components/app_runtime/include",
                "components/app_core/include",
                "components/board_wave_175c/include",
            ],
        )

    def test_runtime_ui_hints_long_press_retry_for_recoverable_asr_error(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_runtime_ui.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    board_wave_175c_display_page_t page =
                        aiqa_runtime_ui_page_for(AIQA_STATE_ERROR, AIQA_ERROR_ASR_FAILED);
                    assert(strcmp(page.status, "ASR") == 0);
                    assert(strcmp(page.hint, "RETRY") == 0);
                    assert(page.is_error);

                    page = aiqa_runtime_ui_page_for(AIQA_STATE_ERROR, AIQA_ERROR_CONFIG_MISSING);
                    assert(strcmp(page.status, "SETUP") == 0);
                    assert(strcmp(page.hint, "PAIR") == 0);
                    return 0;
                }
                """
            ),
            [
                "components/app_runtime/src/aiqa_runtime_ui.c",
                "components/app_core/src/aiqa_dialogue_view.c",
            ],
            [
                "components/app_runtime/include",
                "components/app_core/include",
                "components/board_wave_175c/include",
            ],
        )

    def test_provider_capabilities_for_default_qwen(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_provider.h"
                #include <assert.h>

                int main(void) {
                    const aiqa_provider_caps_t *caps = aiqa_provider_caps_for(AIQA_PROVIDER_DASHSCOPE_CHAT);
                    assert(caps != 0);
                    assert(caps->supports_chat_stream);
                    assert(caps->supports_reasoning_controls);
                    assert(aiqa_provider_is_known(AIQA_PROVIDER_MINIMAX_CHAT));
                    assert(aiqa_provider_caps_for("unknown") == 0);
                    return 0;
                }
                """
            ),
            ["components/provider_common/src/aiqa_provider.c"],
            ["components/provider_common/include"],
        )

    def test_chat_protocol_builds_qwen_request_without_leaking_secret(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_chat_protocol.h"
                #include "aiqa_config.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_config_t config = aiqa_config_default();
                    aiqa_chat_options_t options = {
                        .stream = false,
                        .hide_reasoning = true,
                        .max_completion_tokens = 96,
                    };

                    char url[AIQA_CHAT_ENDPOINT_MAX_LEN] = {0};
                    aiqa_chat_status_t status = aiqa_chat_build_endpoint_url(
                        config.base_url,
                        url,
                        sizeof(url));
                    assert(status == AIQA_CHAT_OK);
                    assert(strcmp(url, "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions") == 0);

                    char body[AIQA_CHAT_REQUEST_MAX_LEN] = {0};
                    status = aiqa_chat_build_request_json(
                        &config,
                        &options,
                        "Hello \\"pet\\"!\\nCan you explain rain?",
                        body,
                        sizeof(body));
                    assert(status == AIQA_CHAT_OK);
                    assert(strstr(body, "\\"model\\":\\"qwen3.7-max\\"") != 0);
                    assert(strstr(body, "\\"role\\":\\"system\\"") != 0);
                    assert(strstr(body, "personal voice assistant") != 0);
                    assert(strstr(body, "Do not call the user") != 0);
                    assert(strstr(body, "AI electronic pet") == 0);
                    assert(strstr(body, "主人") == 0);
                    assert(strstr(body, "\\"stream\\":false") != 0);
                    assert(strstr(body, "\\"enable_thinking\\":false") != 0);
                    assert(strstr(body, "\\"max_tokens\\":96") != 0);
                    assert(strstr(body, "Hello") != 0);
                    assert(strstr(body, "Can you explain rain?") != 0);
                    assert(strstr(body, "\\\\n") != 0);
                    assert(strstr(body, "sk-") == 0);

                    char reply[128] = {0};
                    status = aiqa_chat_parse_response_text(
                        "{\\"choices\\":[{\\"message\\":{\\"role\\":\\"assistant\\",\\"content\\":\\"Rain comes from clouds.\\nWant a tiny experiment?\\"}}]}",
                        reply,
                        sizeof(reply));
                    assert(status == AIQA_CHAT_OK);
                    assert(strcmp(reply, "Rain comes from clouds.\\nWant a tiny experiment?") == 0);

                    assert(aiqa_chat_status_from_http_status(200) == AIQA_CHAT_OK);
                    assert(aiqa_chat_status_from_http_status(401) == AIQA_CHAT_ERR_AUTH);
                    assert(aiqa_chat_status_from_http_status(429) == AIQA_CHAT_ERR_RATE_LIMITED);
                    assert(aiqa_chat_status_from_http_status(504) == AIQA_CHAT_ERR_TIMEOUT);
                    assert(aiqa_chat_status_from_http_status(500) == AIQA_CHAT_ERR_PROVIDER);
                    return 0;
                }
                """
            ),
            [
                "components/chat_client/src/aiqa_chat_protocol.c",
                "components/config_store/src/aiqa_config.c",
                "components/provider_common/src/aiqa_provider.c",
            ],
            [
                "components/chat_client/include",
                "components/config_store/include",
                "components/provider_common/include",
            ],
        )

    def test_chat_protocol_builds_qwen_stream_request_and_parses_sse_delta(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_chat_protocol.h"
                #include "aiqa_config.h"
                #include "aiqa_language.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_config_t config = aiqa_config_default();
                    aiqa_chat_options_t options = {
                        .stream = true,
                        .hide_reasoning = true,
                        .max_completion_tokens = 64,
                        .response_language = aiqa_language_chat_code(AIQA_DIALOGUE_LANGUAGE_CHINESE),
                    };

                    char body[AIQA_CHAT_REQUEST_MAX_LEN] = {0};
                    aiqa_chat_status_t status = aiqa_chat_build_request_json(
                        &config,
                        &options,
                        "tell me a small pet fact",
                        body,
                        sizeof(body));
                    assert(status == AIQA_CHAT_OK);
                    assert(strstr(body, "\\"stream\\":true") != 0);
                    assert(strstr(body, "\\"enable_thinking\\":false") != 0);
                    assert(strstr(body, "Simplified Chinese") != 0);
                    assert(strstr(body, "不要用日语") != 0);
                    assert(strstr(body, "12+28=40") != 0);
                    assert(strstr(body, "sk-") == 0);

                    options.conversation_context = "User: 我的名字是小明\\nPet: 我记住啦";
                    options.assistant_profile_context = "Assistant profile: name=小智, gender=female.";
                    status = aiqa_chat_build_request_json(
                        &config,
                        &options,
                        "我叫什么名字",
                        body,
                        sizeof(body));
                    assert(status == AIQA_CHAT_OK);
                    assert(strstr(body, "Recent conversation memory") != 0);
                    assert(strstr(body, "Assistant profile") != 0);
                    assert(strstr(body, "小智") != 0);
                    assert(strstr(body, "我的名字是小明") != 0);
                    assert(strstr(body, "我叫什么名字") != 0);

                    options.response_language = aiqa_language_chat_code(AIQA_DIALOGUE_LANGUAGE_ENGLISH);
                    options.conversation_context = NULL;
                    options.assistant_profile_context = NULL;
                    status = aiqa_chat_build_request_json(
                        &config,
                        &options,
                        "please use english",
                        body,
                        sizeof(body));
                    assert(status == AIQA_CHAT_OK);
                    assert(strstr(body, "Reply in English") != 0);

                    char delta[64] = {0};
                    status = aiqa_chat_parse_stream_delta_text(
                        "data: {\\"choices\\":[{\\"delta\\":{\\"role\\":\\"assistant\\",\\"content\\":\\"Hello pet!\\\\n\\"}}]}\\n\\n",
                        delta,
                        sizeof(delta));
                    assert(status == AIQA_CHAT_OK);
                    assert(strcmp(delta, "Hello pet!\\n") == 0);

                    delta[0] = '\\0';
                    status = aiqa_chat_parse_stream_delta_text("data: [DONE]\\n\\n", delta, sizeof(delta));
                    assert(status == AIQA_CHAT_ERR_PARSE);
                    assert(delta[0] == '\\0');
                    return 0;
                }
                """
            ),
            [
                "components/chat_client/src/aiqa_chat_protocol.c",
                "components/app_core/src/aiqa_language.c",
                "components/config_store/src/aiqa_config.c",
                "components/provider_common/src/aiqa_provider.c",
            ],
            [
                "components/chat_client/include",
                "components/app_core/include",
                "components/config_store/include",
                "components/provider_common/include",
            ],
        )

    def test_language_switch_commands_detect_chinese_and_english(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_language.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_dialogue_language_t language = AIQA_DIALOGUE_LANGUAGE_ENGLISH;
                    assert(aiqa_language_detect_switch_command("使用中文与我交流", &language));
                    assert(language == AIQA_DIALOGUE_LANGUAGE_CHINESE);
                    assert(strcmp(aiqa_language_confirmation(language), "好的，我会用中文和你交流。") == 0);
                    assert(strcmp(aiqa_language_display_label(language), "CHINESE MODE") == 0);
                    assert(strcmp(aiqa_language_chat_code(language), "zh") == 0);

                    language = AIQA_DIALOGUE_LANGUAGE_CHINESE;
                    assert(aiqa_language_detect_switch_command("please speak English with me", &language));
                    assert(language == AIQA_DIALOGUE_LANGUAGE_ENGLISH);
                    assert(strcmp(aiqa_language_confirmation(language), "Sure, I will speak English with you.") == 0);
                    assert(strcmp(aiqa_language_display_label(language), "ENGLISH MODE") == 0);
                    assert(strcmp(aiqa_language_chat_code(language), "en") == 0);

                    assert(!aiqa_language_detect_switch_command("中文和英文有什么区别", &language));
                    assert(!aiqa_language_detect_switch_command("苹果用英文怎么说", &language));
                    assert(!aiqa_language_detect_switch_command("how do you say hello in English", &language));
                    assert(!aiqa_language_detect_switch_command("", &language));
                    assert(!aiqa_language_detect_switch_command(0, &language));
                    assert(!aiqa_language_detect_switch_command("use english", 0));
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_language.c"],
            ["components/app_core/include"],
        )

    def test_chat_protocol_builds_minimax_request_without_qwen_only_fields(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_chat_protocol.h"
                #include "aiqa_config.h"
                #include "aiqa_provider.h"
                #include <assert.h>
                #include <stdio.h>
                #include <string.h>

                int main(void) {
                    aiqa_config_t config = aiqa_config_default();
                    (void)snprintf(config.active_provider, sizeof(config.active_provider), "%s",
                                   AIQA_PROVIDER_MINIMAX_CHAT);
                    (void)snprintf(config.model, sizeof(config.model), "%s", AIQA_DEFAULT_MINIMAX_MODEL);
                    (void)snprintf(config.base_url, sizeof(config.base_url), "%s", "https://api.minimax.io/v1");
                    assert(aiqa_config_validate(&config) == AIQA_CONFIG_OK);

                    aiqa_chat_options_t options = {
                        .stream = false,
                        .hide_reasoning = true,
                        .max_completion_tokens = 128,
                    };

                    char url[AIQA_CHAT_ENDPOINT_MAX_LEN] = {0};
                    assert(aiqa_chat_build_endpoint_url(config.base_url, url, sizeof(url)) == AIQA_CHAT_OK);
                    assert(strcmp(url, "https://api.minimax.io/v1/chat/completions") == 0);

                    char body[AIQA_CHAT_REQUEST_MAX_LEN] = {0};
                    aiqa_chat_status_t status = aiqa_chat_build_request_json(
                        &config,
                        &options,
                        "你好，小宠物",
                        body,
                        sizeof(body));
                    assert(status == AIQA_CHAT_OK);
                    assert(strstr(body, "\\"model\\":\\"MiniMax-M3\\"") != 0);
                    assert(strstr(body, "\\"max_completion_tokens\\":128") != 0);
                    assert(strstr(body, "\\"reasoning_split\\":true") != 0);
                    assert(strstr(body, "\\"enable_thinking\\"") == 0);
                    assert(strstr(body, "\\"max_tokens\\"") == 0);
                    assert(strstr(body, "sk-") == 0);
                    return 0;
                }
                """
            ),
            [
                "components/chat_client/src/aiqa_chat_protocol.c",
                "components/config_store/src/aiqa_config.c",
                "components/provider_common/src/aiqa_provider.c",
            ],
            [
                "components/chat_client/include",
                "components/config_store/include",
                "components/provider_common/include",
            ],
        )

    def test_tts_protocol_builds_streaming_dashscope_qwen_pcm_request_and_parses_audio_delta(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_tts_protocol.h"
                #include "aiqa_config.h"
                #include <assert.h>
                #include <string.h>

                int main(void) {
                    aiqa_config_t config = aiqa_config_default();
                    aiqa_tts_options_t options = {
                        .voice = config.tts_voice,
                        .format = "pcm",
                        .sample_rate_hz = 24000,
                        .stream = true,
                    };

                    char url[AIQA_TTS_ENDPOINT_MAX_LEN] = {0};
                    aiqa_tts_status_t status = aiqa_tts_build_endpoint_url(
                        config.tts_base_url,
                        url,
                        sizeof(url));
                    assert(status == AIQA_TTS_OK);
                    assert(strcmp(url, "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation") == 0);

                    status = aiqa_tts_build_endpoint_url(
                        "https://dashscope.aliyuncs.com/compatible-mode/v1",
                        url,
                        sizeof(url));
                    assert(status == AIQA_TTS_OK);
                    assert(strcmp(url, "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation") == 0);

                    char body[AIQA_TTS_REQUEST_MAX_LEN] = {0};
                    status = aiqa_tts_build_request_json(
                        &config,
                        &options,
                        "PET HAPPY TO HELP",
                        body,
                        sizeof(body));
                    assert(status == AIQA_TTS_OK);
                    assert(strstr(body, "\\"model\\":\\"qwen3-tts-flash\\"") != 0);
                    assert(strstr(body, "\\"input\\":{") != 0);
                    assert(strstr(body, "\\"text\\":\\"PET HAPPY TO HELP\\"") != 0);
                    assert(strstr(body, "\\"voice\\":\\"Cherry\\"") != 0);
                    assert(strstr(body, "\\"language_type\\":\\"Auto\\"") != 0);
                    assert(strstr(body, "\\"stream\\"") == 0);
                    assert(strstr(body, "\\"modalities\\"") == 0);
                    assert(strstr(body, "\\"format\\"") == 0);
                    assert(strstr(body, "sk-") == 0);

                    char audio_b64[64] = {0};
                    status = aiqa_tts_parse_stream_audio_data(
                        "data: {\\"output\\":{\\"audio\\":{\\"data\\":\\"QUJDRA==\\",\\"url\\":\\"\\"}}}\\n\\n",
                        audio_b64,
                        sizeof(audio_b64));
                    assert(status == AIQA_TTS_OK);
                    assert(strcmp(audio_b64, "QUJDRA==") == 0);

                    char tiny_b64[4] = {0};
                    status = aiqa_tts_parse_stream_audio_data(
                        "data: {\\"output\\":{\\"audio\\":{\\"data\\":\\"QUJDRA==\\",\\"url\\":\\"\\"}}}\\n\\n",
                        tiny_b64,
                        sizeof(tiny_b64));
                    assert(status == AIQA_TTS_ERR_BUFFER_TOO_SMALL);
                    assert(tiny_b64[0] == '\\0');

                    audio_b64[0] = 'x';
                    status = aiqa_tts_parse_stream_audio_data(
                        "data: {\\"output\\":{\\"audio\\":{\\"data\\":\\"\\",\\"url\\":\\"https://example.com/out.wav\\"}}}\\n\\n",
                        audio_b64,
                        sizeof(audio_b64));
                    assert(status == AIQA_TTS_OK);
                    assert(audio_b64[0] == '\\0');

                    audio_b64[0] = '\\0';
                    status = aiqa_tts_parse_stream_audio_data("data: [DONE]\\n\\n", audio_b64, sizeof(audio_b64));
                    assert(status == AIQA_TTS_ERR_PARSE);
                    assert(audio_b64[0] == '\\0');
                    return 0;
                }
                """
            ),
            [
                "components/tts_client/src/aiqa_tts_protocol.c",
                "components/config_store/src/aiqa_config.c",
                "components/provider_common/src/aiqa_provider.c",
            ],
            [
                "components/tts_client/include",
                "components/config_store/include",
                "components/provider_common/include",
            ],
        )

    def test_asr_protocol_builds_static_sample_request_without_leaking_secret(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_asr_protocol.h"
                #include "aiqa_config.h"
                #include <assert.h>
                #include <stdint.h>
                #include <stdio.h>
                #include <string.h>

                int main(void) {
                    aiqa_config_t config = aiqa_config_default();
                    aiqa_asr_options_t options = {
                        .audio_ref = AIQA_ASR_STATIC_SAMPLE_URL,
                        .language_hint = "zh",
                        .enable_itn = true,
                    };

                    char url[AIQA_ASR_ENDPOINT_MAX_LEN] = {0};
                    aiqa_asr_status_t status = aiqa_asr_build_endpoint_url(
                        config.asr_base_url,
                        url,
                        sizeof(url));
                    assert(status == AIQA_ASR_OK);
                    assert(strcmp(url, "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions") == 0);

                    char body[AIQA_ASR_REQUEST_MAX_LEN] = {0};
                    status = aiqa_asr_build_request_json(&config, &options, body, sizeof(body));
                    assert(status == AIQA_ASR_OK);
                    assert(strstr(body, "\\"model\\":\\"qwen3-asr-flash\\"") != 0);
                    assert(strstr(body, "\\"type\\":\\"input_audio\\"") != 0);
                    assert(strstr(body, AIQA_ASR_STATIC_SAMPLE_URL) != 0);
                    assert(strstr(body, "\\"language\\":\\"zh\\"") != 0);
                    assert(strstr(body, "\\"enable_itn\\":true") != 0);
                    assert(strstr(body, "sk-") == 0);

                    char transcript[128] = {0};
                    status = aiqa_asr_parse_transcript_text(
                        "{\\"choices\\":[{\\"message\\":{\\"content\\":\\"今天天气不错\\"}}]}",
                        transcript,
                        sizeof(transcript));
                    assert(status == AIQA_ASR_OK);
                    assert(strcmp(transcript, "今天天气不错") == 0);

                    assert(aiqa_asr_status_from_http_status(200) == AIQA_ASR_OK);
                    assert(aiqa_asr_status_from_http_status(401) == AIQA_ASR_ERR_AUTH);
                    assert(aiqa_asr_status_from_http_status(429) == AIQA_ASR_ERR_RATE_LIMITED);
                    assert(aiqa_asr_status_from_http_status(504) == AIQA_ASR_ERR_TIMEOUT);
                    assert(aiqa_asr_status_from_http_status(500) == AIQA_ASR_ERR_PROVIDER);

                    uint8_t wav_header[AIQA_ASR_WAV_HEADER_BYTES] = {0};
                    status = aiqa_asr_write_wav_header(wav_header, 16000, 16, 1, 32000);
                    assert(status == AIQA_ASR_OK);
                    assert(memcmp(wav_header, "RIFF", 4) == 0);
                    assert(memcmp(wav_header + 8, "WAVE", 4) == 0);
                    assert(memcmp(wav_header + 12, "fmt ", 4) == 0);
                    assert(memcmp(wav_header + 36, "data", 4) == 0);
                    assert(wav_header[24] == 0x80);
                    assert(wav_header[25] == 0x3e);
                    assert(wav_header[34] == 16);
                    assert(wav_header[40] == 0x00);
                    assert(wav_header[41] == 0x7d);
                    assert(aiqa_asr_wav_total_bytes(32000) == 32044);
                    assert(aiqa_asr_base64_encoded_len(32044) == 42728);

                    aiqa_asr_options_t data_uri_options = {
                        .audio_ref = 0,
                        .language_hint = "zh",
                        .enable_itn = true,
                        .audio_source_kind = AIQA_AUDIO_SOURCE_DATA_URI,
                        .audio_bytes = 32044,
                    };
                    assert(aiqa_asr_validate_audio_source(&config, &data_uri_options) == AIQA_ASR_OK);

                    char prefix[AIQA_ASR_REQUEST_PART_MAX_LEN] = {0};
                    char suffix[AIQA_ASR_REQUEST_PART_MAX_LEN] = {0};
                    assert(aiqa_asr_build_data_uri_request_prefix_json(&config, prefix, sizeof(prefix)) == AIQA_ASR_OK);
                    assert(aiqa_asr_build_request_suffix_json(&data_uri_options, suffix, sizeof(suffix)) == AIQA_ASR_OK);
                    assert(strstr(prefix, "\\"data\\":\\"data:audio/wav;base64,") != 0);
                    assert(strstr(suffix, "\\"enable_itn\\":true") != 0);
                    char streamed[1024] = {0};
                    assert(snprintf(streamed, sizeof(streamed), "%s%s\\"%s", prefix, "QUJD", suffix) > 0);
                    assert(strstr(streamed, "\\"data\\":\\"data:audio/wav;base64,QUJD\\"") != 0);
                    assert(strstr(streamed, "sk-") == 0);

                    data_uri_options.audio_bytes = 11 * 1024 * 1024;
                    assert(aiqa_asr_validate_audio_source(&config, &data_uri_options) ==
                           AIQA_ASR_ERR_UNSUPPORTED_PROVIDER);

                    aiqa_config_t chat_provider_as_asr = config;
                    assert(snprintf(chat_provider_as_asr.asr_provider,
                                    sizeof(chat_provider_as_asr.asr_provider),
                                    "%s",
                                    "dashscope_openai_chat") > 0);
                    assert(snprintf(chat_provider_as_asr.asr_model,
                                    sizeof(chat_provider_as_asr.asr_model),
                                    "%s",
                                    "qwen3.7-max") > 0);
                    data_uri_options.audio_bytes = 32044;
                    assert(aiqa_asr_validate_audio_source(&chat_provider_as_asr, &data_uri_options) ==
                           AIQA_ASR_ERR_UNSUPPORTED_PROVIDER);
                    return 0;
                }
                """
            ),
            [
                "components/asr_client/src/aiqa_asr_protocol.c",
                "components/config_store/src/aiqa_config.c",
                "components/provider_common/src/aiqa_provider.c",
            ],
            [
                "components/asr_client/include",
                "components/config_store/include",
                "components/provider_common/include",
            ],
        )

    def test_board_i2c_required_device_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "board_wave_175c_contract.h"
                #include <assert.h>
                #include <stdint.h>

                int main(void) {
                    const board_wave_175c_i2c_device_t *devices = board_wave_175c_expected_i2c_devices();
                    assert(devices != 0);
                    assert(board_wave_175c_expected_i2c_device_count() >= 5);

                    uint8_t present[] = {0x34, 0x40, 0x18};
                    uint32_t mask = board_wave_175c_detected_mask_from_addresses(present, 3);
                    assert((mask & BOARD_WAVE_175C_I2C_AXP2101) != 0);
                    assert((mask & BOARD_WAVE_175C_I2C_ES7210) != 0);
                    assert((mask & BOARD_WAVE_175C_I2C_ES8311) != 0);
                    assert(board_wave_175c_has_required_i2c_devices(mask));

                    uint8_t missing_codec[] = {0x34, 0x40};
                    mask = board_wave_175c_detected_mask_from_addresses(missing_codec, 2);
                    assert(!board_wave_175c_has_required_i2c_devices(mask));
                    assert((board_wave_175c_missing_required_mask(mask) & BOARD_WAVE_175C_I2C_ES8311) != 0);
                    return 0;
                }
                """
            ),
            ["components/board_wave_175c/src/board_wave_175c_contract.c"],
            ["components/board_wave_175c/include"],
        )

    def test_board_power_status_decodes_axp2101_registers(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "board_wave_175c_power.h"
                #include <assert.h>

                int main(void) {
                    board_wave_175c_power_status_t status = {0};
                    assert(board_wave_175c_power_decode_axp2101_status(
                        0x28, 0x02, 68, &status));
                    assert(status.battery_present);
                    assert(status.vbus_good);
                    assert(status.percent == 68);
                    assert(status.charging_state == BOARD_WAVE_175C_CHARGING_ACTIVE);

                    assert(board_wave_175c_power_decode_axp2101_status(
                        0x20, 0x00, 100, &status));
                    assert(status.battery_present);
                    assert(!status.vbus_good);
                    assert(status.percent == 100);
                    assert(status.charging_state == BOARD_WAVE_175C_CHARGING_DISCHARGING);

                    assert(board_wave_175c_power_decode_axp2101_status(
                        0x08, 0x00, 255, &status));
                    assert(!status.battery_present);
                    assert(status.vbus_good);
                    assert(status.percent == 0);
                    return 0;
                }
                """
            ),
            ["components/board_wave_175c/src/board_wave_175c_power.c"],
            ["components/board_wave_175c/include"],
        )

    def test_board_display_pin_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "board_wave_175c_pins.h"
                #include <assert.h>

                int main(void) {
                    assert(WAVE_175C_LCD_WIDTH == 466);
                    assert(WAVE_175C_LCD_HEIGHT == 466);
                    assert(WAVE_175C_LCD_BITS_PER_PIXEL == 16);
                    assert(WAVE_175C_LCD_RESET == 1);
                    assert(WAVE_175C_TOUCH_RST == 2);
                    assert(WAVE_175C_ES7210_MCLK == 16);
                    assert(WAVE_175C_LCD_TRANSFER_ROWS > 0);
                    assert(WAVE_175C_LCD_TRANSFER_ROWS < WAVE_175C_LCD_HEIGHT);
                    return 0;
                }
                """
            ),
            [],
            ["components/board_wave_175c/include"],
        )

    def test_round_display_pet_layout_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "board_wave_175c_display_contract.h"
                #include <assert.h>

                int main(void) {
                    assert(board_wave_175c_display_safe_center_x() == 233);
                    assert(board_wave_175c_display_safe_center_y() == 233);
                    assert(board_wave_175c_display_safe_radius() <= 205);
                    assert(!board_wave_175c_display_rect_inside_safe_circle(0, 0, 466, 40));

                    board_wave_175c_display_rect_t title = {0};
                    assert(board_wave_175c_display_centered_text_rect(6, 2, 42, &title));
                    assert(board_wave_175c_display_rect_inside_safe_circle(title.x, title.y, title.width, title.height));

                    board_wave_175c_display_rect_t status = {0};
                    assert(board_wave_175c_display_centered_text_rect(14, 4, 280, &status));
                    assert(board_wave_175c_display_rect_inside_safe_circle(status.x, status.y, status.width, status.height));

                    board_wave_175c_display_rect_t detail = {0};
                    assert(board_wave_175c_display_centered_text_rect(23, 2, 334, &detail));
                    assert(board_wave_175c_display_rect_inside_safe_circle(detail.x, detail.y, detail.width, detail.height));

                    assert(board_wave_175c_pet_face_rect_inside_safe_circle());
                    assert(board_wave_175c_pet_layout_is_circle_safe());
                    return 0;
                }
                """
            ),
            ["components/board_wave_175c/src/board_wave_175c_display_contract.c"],
            ["components/board_wave_175c/include"],
        )

    def test_center_pixel_pet_sprite_layout_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "board_wave_175c_pet_sprite.h"
                #include <assert.h>
                #include <string.h>

                static size_t count_visible_pixels(const uint16_t *pixels) {
                    size_t count = 0;
                    for (size_t index = 0;
                         index < BOARD_WAVE_175C_PET_SPRITE_WIDTH *
                                 BOARD_WAVE_175C_PET_SPRITE_HEIGHT;
                         ++index) {
                        if (pixels[index] != 0) {
                            ++count;
                        }
                    }
                    return count;
                }

                int main(void) {
                    assert(BOARD_WAVE_175C_PET_SPRITE_WIDTH == 160);
                    assert(BOARD_WAVE_175C_PET_SPRITE_HEIGHT == 160);
                    assert(BOARD_WAVE_175C_PET_SPRITE_SCALE == 1);
                    assert((BOARD_WAVE_175C_PET_SPRITE_WIDTH *
                            BOARD_WAVE_175C_PET_SPRITE_HEIGHT *
                            sizeof(uint16_t)) <= 52 * 1024);

                    static uint16_t first_frame[BOARD_WAVE_175C_PET_SPRITE_WIDTH *
                                                BOARD_WAVE_175C_PET_SPRITE_HEIGHT];
                    static uint16_t pixels[BOARD_WAVE_175C_PET_SPRITE_WIDTH *
                                           BOARD_WAVE_175C_PET_SPRITE_HEIGHT];
                    for (int expression = 0;
                         expression < BOARD_WAVE_175C_PET_EXPRESSION_COUNT;
                         ++expression) {
                        const board_wave_175c_pet_sprite_t *sprite =
                            board_wave_175c_pet_sprite_for_expression((board_wave_175c_pet_expression_t)expression);
                        assert(sprite != 0);
                        assert(sprite->pixel_count == BOARD_WAVE_175C_PET_SPRITE_WIDTH *
                                                BOARD_WAVE_175C_PET_SPRITE_HEIGHT);
                        assert(sprite->width == BOARD_WAVE_175C_PET_SPRITE_WIDTH);
                        assert(sprite->height == BOARD_WAVE_175C_PET_SPRITE_HEIGHT);
                        assert(sprite->scale == BOARD_WAVE_175C_PET_SPRITE_SCALE);
                        assert(sprite->frame_count >= 2);
                        assert(board_wave_175c_pet_sprite_render(
                            sprite, 0, first_frame,
                            sizeof(first_frame) / sizeof(first_frame[0])));
                        assert(first_frame[(BOARD_WAVE_175C_PET_SPRITE_HEIGHT / 2) *
                                           BOARD_WAVE_175C_PET_SPRITE_WIDTH +
                                           BOARD_WAVE_175C_PET_SPRITE_WIDTH / 2] != 0);

                        size_t first_visible = count_visible_pixels(first_frame);
                        assert(first_visible > 2500);
                        assert(first_visible < BOARD_WAVE_175C_PET_SPRITE_WIDTH *
                                               BOARD_WAVE_175C_PET_SPRITE_HEIGHT);

                        bool has_frame_delta = false;
                        for (size_t frame = 1; frame < sprite->frame_count; ++frame) {
                            assert(board_wave_175c_pet_sprite_render(
                                sprite, frame, pixels,
                                sizeof(pixels) / sizeof(pixels[0])));
                            size_t visible = count_visible_pixels(pixels);
                            assert(visible > 2500);
                            assert(visible < BOARD_WAVE_175C_PET_SPRITE_WIDTH *
                                             BOARD_WAVE_175C_PET_SPRITE_HEIGHT);
                            if (memcmp(first_frame, pixels, sizeof(pixels)) != 0) {
                                has_frame_delta = true;
                            }
                        }
                        assert(has_frame_delta);

                        board_wave_175c_display_rect_t rect = {0};
                        assert(board_wave_175c_pet_sprite_rect(sprite, &rect));
                        assert(rect.x == 153);
                        assert(rect.y == 102);
                        assert(rect.width == 160);
                        assert(rect.height == 160);
                        assert(rect.x >= 0);
                        assert(rect.y >= 0);
                        assert(board_wave_175c_display_rect_inside_safe_circle(
                            rect.x, rect.y, rect.width, rect.height));
                    }
                    const board_wave_175c_pet_sprite_t *fallback =
                        board_wave_175c_pet_sprite_for_expression((board_wave_175c_pet_expression_t)999);
                    assert(fallback->expression == BOARD_WAVE_175C_PET_EXPRESSION_IDLE);
                    assert(board_wave_175c_pet_sprite_layout_is_circle_safe());
                    return 0;
                }
                """
            ),
            [
                "components/board_wave_175c/src/board_wave_175c_display_contract.c",
                "components/board_wave_175c/src/board_wave_175c_pet_sprite.c",
            ],
            ["components/board_wave_175c/include"],
        )

    def test_audio_capture_budget_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_audio_capture.h"
                #include "aiqa_audio_capture_hw.h"
                #include <assert.h>

                int main(void) {
                    aiqa_audio_capture_config_t config = aiqa_audio_capture_default_config();
                    assert(config.sample_rate_hz == 24000);
                    assert(config.bits_per_sample == 16);
                    assert(config.channels == 1);
                    assert(aiqa_audio_pcm_bytes_per_second(&config) == 48000);
                    assert(config.max_record_seconds <= 20);
                    assert(config.max_pcm_bytes == aiqa_audio_pcm_bytes_per_second(&config) * config.max_record_seconds);
                    assert(aiqa_audio_capture_config_is_safe(&config));

                    config.ring_buffer_bytes = 0;
                    assert(!aiqa_audio_capture_config_is_safe(&config));

                    config = aiqa_audio_capture_default_config();
                    config.ring_buffer_bytes = config.max_pcm_bytes + 1;
                    assert(!aiqa_audio_capture_config_is_safe(&config));

                    config = aiqa_audio_capture_default_config();
                    config.max_record_seconds = 600;
                    config.max_pcm_bytes = aiqa_audio_pcm_bytes_per_second(&config) * config.max_record_seconds;
                    assert(!aiqa_audio_capture_config_is_safe(&config));

                    aiqa_audio_capture_hw_config_t hw_config = aiqa_audio_capture_hw_default_config();
                    assert(hw_config.sample_rate_hz == 24000);
                    assert(hw_config.bits_per_sample == 16);
                    assert(hw_config.source_channels == 4);
                    assert(hw_config.output_channels == 1);
                    assert(hw_config.chunk_frames == 256);
                    assert(aiqa_audio_capture_hw_config_is_safe(&hw_config));

                    hw_config.source_channels = 2;
                    assert(!aiqa_audio_capture_hw_config_is_safe(&hw_config));

                    hw_config = aiqa_audio_capture_hw_default_config();
                    hw_config.mic_gain_db = 60;
                    assert(!aiqa_audio_capture_hw_config_is_safe(&hw_config));

                    hw_config = aiqa_audio_capture_hw_default_config();
                    hw_config.chunk_frames = 0;
                    assert(!aiqa_audio_capture_hw_config_is_safe(&hw_config));
                    return 0;
                }
                """
            ),
            ["components/audio_capture/src/aiqa_audio_capture.c"],
            ["components/audio_capture/include"],
        )

    def test_audio_playback_budget_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_audio_playback.h"
                #include <assert.h>

                int main(void) {
                    aiqa_audio_playback_config_t config = aiqa_audio_playback_default_config();
                    assert(config.sample_rate_hz == 24000);
                    assert(config.bits_per_sample == 16);
                    assert(config.channels == 1);
                    assert(config.volume_percent == 10);
                    assert(config.chunk_bytes == 1024);
                    assert(aiqa_audio_playback_config_is_safe(&config));

                    config.sample_rate_hz = 44100;
                    assert(!aiqa_audio_playback_config_is_safe(&config));

                    config = aiqa_audio_playback_default_config();
                    config.bits_per_sample = 24;
                    assert(!aiqa_audio_playback_config_is_safe(&config));

                    config = aiqa_audio_playback_default_config();
                    config.volume_percent = 101;
                    assert(!aiqa_audio_playback_config_is_safe(&config));

                    config = aiqa_audio_playback_default_config();
                    config.volume_percent = 0;
                    assert(aiqa_audio_playback_config_is_safe(&config));

                    config = aiqa_audio_playback_default_config();
                    config.chunk_bytes = 0;
                    assert(!aiqa_audio_playback_config_is_safe(&config));
                    return 0;
                }
                """
            ),
            ["components/audio_capture/src/aiqa_audio_playback.c"],
            ["components/audio_capture/include"],
        )

    def test_ptt_button_long_press_and_timeout_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_ptt_button.h"
                #include <assert.h>

                int main(void) {
                    aiqa_ptt_policy_t policy = aiqa_ptt_default_policy();
                    assert(aiqa_ptt_policy_is_safe(&policy));
                    assert(policy.long_press_ms > policy.debounce_ms);

                    aiqa_ptt_button_t button;
                    aiqa_ptt_button_init(&button);

                    assert(aiqa_ptt_button_update(&button, &policy, false, 0) == AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 10) == AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, false, 40) == AIQA_PTT_OUTPUT_NONE);

                    assert(aiqa_ptt_button_update(&button, &policy, true, 100) == AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 100 + policy.debounce_ms) ==
                           AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 100 + policy.long_press_ms - 1) ==
                           AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 100 + policy.long_press_ms) ==
                           AIQA_PTT_OUTPUT_PRESS_START);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 100 + policy.long_press_ms + 20) ==
                           AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, false, 100 + policy.long_press_ms + 40) ==
                           AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, false, 100 + policy.long_press_ms + 40 + policy.debounce_ms) ==
                           AIQA_PTT_OUTPUT_PRESS_END);

                    aiqa_ptt_button_init(&button);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 1000) == AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 1000 + policy.long_press_ms) ==
                           AIQA_PTT_OUTPUT_PRESS_START);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 1000 + policy.long_press_ms + policy.max_record_ms) ==
                           AIQA_PTT_OUTPUT_AUDIO_TOO_LONG);
                    assert(aiqa_ptt_button_update(&button, &policy, true, 1000 + policy.long_press_ms + policy.max_record_ms + 20) ==
                           AIQA_PTT_OUTPUT_NONE);
                    assert(aiqa_ptt_button_update(&button, &policy, false, 1000 + policy.long_press_ms + policy.max_record_ms + 40) ==
                           AIQA_PTT_OUTPUT_NONE);
                    return 0;
                }
                """
            ),
            ["components/app_core/src/aiqa_ptt_button.c"],
            ["components/app_core/include"],
        )

    def test_runtime_config_rejects_unapproved_hosts_and_missing_secrets(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_config.h"
                #include "aiqa_provider.h"
                #include <assert.h>
                #include <stdio.h>

                int main(void) {
                    aiqa_config_t config = aiqa_config_default();
                    assert(aiqa_config_validate(&config) == AIQA_CONFIG_OK);
                    assert(aiqa_provider_model_allowed(config.asr_provider, config.asr_model));

                    (void)snprintf(config.base_url, sizeof(config.base_url), "%s",
                                   "https://evil.example.com/compatible-mode/v1");
                    assert(aiqa_config_validate(&config) == AIQA_CONFIG_ERR_BASE_URL);

                    config = aiqa_config_default();
                    (void)snprintf(config.base_url, sizeof(config.base_url), "%s",
                                   "https://workspace-id.ap-southeast-1.maas.aliyuncs.com/compatible-mode/v1");
                    assert(aiqa_config_validate(&config) == AIQA_CONFIG_OK);

                    (void)snprintf(config.active_provider, sizeof(config.active_provider), "%s",
                                   AIQA_PROVIDER_MINIMAX_CHAT);
                    (void)snprintf(config.model, sizeof(config.model), "%s",
                                   AIQA_DEFAULT_MINIMAX_MODEL);
                    assert(aiqa_config_validate(&config) == AIQA_CONFIG_ERR_BASE_URL);

                    config = aiqa_config_default();
                    (void)snprintf(config.asr_base_url, sizeof(config.asr_base_url), "%s",
                                   "https://evil.example.com/compatible-mode/v1");
                    assert(aiqa_config_validate(&config) == AIQA_CONFIG_ERR_BASE_URL);

                    config = aiqa_config_default();
                    (void)snprintf(config.asr_model, sizeof(config.asr_model), "%s", "qwen-plus");
                    assert(aiqa_config_validate(&config) == AIQA_CONFIG_ERR_MODEL);

                    aiqa_secret_config_t secrets = {0};
                    assert(aiqa_secret_config_validate(&secrets) == AIQA_SECRET_ERR_WIFI_SSID);

                    (void)snprintf(secrets.wifi_ssid, sizeof(secrets.wifi_ssid), "%s", "lab-wifi");
                    (void)snprintf(secrets.wifi_password, sizeof(secrets.wifi_password), "%s", "short");
                    (void)snprintf(secrets.chat_api_key, sizeof(secrets.chat_api_key), "%s", "sk-123456");
                    assert(aiqa_secret_config_validate(&secrets) == AIQA_SECRET_ERR_WIFI_PASSWORD);

                    (void)snprintf(secrets.wifi_password, sizeof(secrets.wifi_password), "%s", "correct-horse");
                    secrets.chat_api_key[0] = '\\0';
                    assert(aiqa_secret_config_validate(&secrets) == AIQA_SECRET_ERR_CHAT_API_KEY);

                    (void)snprintf(secrets.chat_api_key, sizeof(secrets.chat_api_key), "%s", "sk-123456");
                    assert(aiqa_secret_config_validate(&secrets) == AIQA_SECRET_OK);
                    return 0;
                }
                """
            ),
            ["components/config_store/src/aiqa_config.c", "components/provider_common/src/aiqa_provider.c"],
            ["components/config_store/include", "components/provider_common/include"],
        )

    def test_network_policy_contract(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_net_policy.h"
                #include <assert.h>

                int main(void) {
                    aiqa_net_policy_t policy = aiqa_net_default_policy();
                    assert(policy.connect_timeout_ms >= 10000);
                    assert(policy.sntp_timeout_ms >= 5000);
                    assert(policy.max_wifi_retries >= 3);
                    assert(policy.sntp_server != 0);

                    assert(!aiqa_net_time_is_valid(0));
                    assert(!aiqa_net_time_is_valid(1600000000));
                    assert(aiqa_net_time_is_valid(1704067200));

                    assert(aiqa_net_retry_delay_ms(0) == 500);
                    assert(aiqa_net_retry_delay_ms(1) == 1000);
                    assert(aiqa_net_retry_delay_ms(3) == 4000);
                    assert(aiqa_net_retry_delay_ms(20) == 5000);
                    return 0;
                }
                """
            ),
            ["components/net_connect/src/aiqa_net_policy.c"],
            ["components/net_connect/include"],
        )

    def test_wifi_update_is_validated_immutable_revisioned_and_redacted(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_config.h"
                #include <assert.h>
                #include <stdint.h>
                #include <stdio.h>
                #include <string.h>

                int main(void) {
                    aiqa_secret_config_t current = {0};
                    (void)snprintf(current.wifi_ssid, sizeof(current.wifi_ssid), "%s", "old-wifi");
                    (void)snprintf(current.wifi_password, sizeof(current.wifi_password), "%s", "old-password");
                    (void)snprintf(current.chat_api_key, sizeof(current.chat_api_key), "%s", "sk-chat-secret");
                    (void)snprintf(current.asr_api_key, sizeof(current.asr_api_key), "%s", "sk-asr-secret");

                    aiqa_wifi_update_t request = {
                        .base_revision = 7,
                        .ssid = "new-wifi",
                        .password_action = AIQA_WIFI_PASSWORD_REPLACE,
                        .password = "new-password",
                    };
                    aiqa_secret_config_t updated = {0};
                    uint32_t next_revision = 0;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) == AIQA_WIFI_UPDATE_OK);
                    assert(next_revision == 8);
                    assert(strcmp(updated.wifi_ssid, "new-wifi") == 0);
                    assert(strcmp(updated.wifi_password, "new-password") == 0);
                    assert(strcmp(updated.chat_api_key, "sk-chat-secret") == 0);
                    assert(strcmp(updated.asr_api_key, "sk-asr-secret") == 0);
                    assert(strcmp(current.wifi_ssid, "old-wifi") == 0);
                    assert(strcmp(current.wifi_password, "old-password") == 0);

                    aiqa_public_wifi_config_t public_view = {0};
                    assert(aiqa_config_build_public_wifi_view(
                        &updated, next_revision, &public_view));
                    assert(strcmp(public_view.ssid, "new-wifi") == 0);
                    assert(public_view.has_password);
                    assert(public_view.revision == 8);
                    assert(sizeof(public_view) < sizeof(updated));

                    request.base_revision = 6;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_REVISION_CONFLICT);

                    request.base_revision = 7;
                    request.ssid = "";
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_SSID);

                    request.ssid = "new-wifi";
                    request.password = "short";
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_PASSWORD);

                    request.password_action = AIQA_WIFI_PASSWORD_KEEP;
                    request.password = "must-not-cross-boundary";
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION);

                    request.password = 0;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) == AIQA_WIFI_UPDATE_OK);
                    assert(strcmp(updated.wifi_password, "old-password") == 0);

                    request.password_action = AIQA_WIFI_PASSWORD_CLEAR;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) == AIQA_WIFI_UPDATE_OK);
                    assert(updated.wifi_password[0] == '\\0');

                    request.password = "must-not-cross-boundary";
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION);

                    request.password_action = (aiqa_wifi_password_action_t)99;
                    request.password = 0;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION);

                    char ssid_32[33];
                    (void)memset(ssid_32, 's', 32);
                    ssid_32[32] = '\\0';
                    request.ssid = ssid_32;
                    request.password_action = AIQA_WIFI_PASSWORD_KEEP;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) == AIQA_WIFI_UPDATE_OK);

                    char ssid_33[34];
                    (void)memset(ssid_33, 's', 33);
                    ssid_33[33] = '\\0';
                    request.ssid = ssid_33;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_SSID);

                    char password_63[64];
                    (void)memset(password_63, 'p', 63);
                    password_63[63] = '\\0';
                    request.ssid = "boundary-network";
                    request.password_action = AIQA_WIFI_PASSWORD_REPLACE;
                    request.password = password_63;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) == AIQA_WIFI_UPDATE_OK);

                    char password_64[65];
                    (void)memset(password_64, 'p', 64);
                    password_64[64] = '\\0';
                    request.password = password_64;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, 7, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_PASSWORD);

                    request.password_action = AIQA_WIFI_PASSWORD_KEEP;
                    request.password = 0;
                    request.base_revision = UINT32_MAX;
                    assert(aiqa_config_prepare_wifi_update(
                        &current, UINT32_MAX, &request, &updated, &next_revision) ==
                        AIQA_WIFI_UPDATE_ERR_REVISION_EXHAUSTED);
                    return 0;
                }
                """
            ),
            ["components/config_store/src/aiqa_config.c", "components/provider_common/src/aiqa_provider.c"],
            ["components/config_store/include", "components/provider_common/include"],
        )


if __name__ == "__main__":
    unittest.main()
