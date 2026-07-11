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
            binary = Path(tmp) / "contract_test"
            test_c.write_text(source, encoding="utf-8")
            cmd = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
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
                    assert(strstr(body, "AI electronic pet") != 0);
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

    def test_asr_protocol_builds_static_sample_request_without_leaking_secret(self):
        self.compile_and_run(
            textwrap.dedent(
                """
                #include "aiqa_asr_protocol.h"
                #include "aiqa_config.h"
                #include <assert.h>
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
                    assert(WAVE_175C_LCD_RESET == 39);
                    assert(WAVE_175C_TOUCH_RST == 40);
                    assert(WAVE_175C_ES7210_MCLK == 42);
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

                int main(void) {
                    for (int expression = BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY;
                         expression <= BOARD_WAVE_175C_PET_EXPRESSION_WORRIED;
                         ++expression) {
                        const board_wave_175c_pet_sprite_t *sprite =
                            board_wave_175c_pet_sprite_for_expression((board_wave_175c_pet_expression_t)expression);
                        assert(sprite != 0);
                        assert(sprite->pixels != 0);
                        assert(sprite->pixel_count == 256);
                        assert(sprite->width == 16);
                        assert(sprite->height == 16);
                        assert(sprite->scale == 9);

                        board_wave_175c_display_rect_t rect = {0};
                        assert(board_wave_175c_pet_sprite_rect(sprite, &rect));
                        assert(rect.x >= 0);
                        assert(rect.y >= 0);
                        assert(board_wave_175c_display_rect_inside_safe_circle(
                            rect.x, rect.y, rect.width, rect.height));
                    }
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
                #include <assert.h>

                int main(void) {
                    aiqa_audio_capture_config_t config = aiqa_audio_capture_default_config();
                    assert(config.sample_rate_hz == 16000);
                    assert(config.bits_per_sample == 16);
                    assert(config.channels == 1);
                    assert(aiqa_audio_pcm_bytes_per_second(&config) == 32000);
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
                    return 0;
                }
                """
            ),
            ["components/audio_capture/src/aiqa_audio_capture.c"],
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


if __name__ == "__main__":
    unittest.main()
