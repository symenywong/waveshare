import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/app_runtime/src/aiqa_runtime.c"
ASR_CLIENT = ROOT / "components/asr_client/src/aiqa_asr_client.c"


def _function_region(source: str, name: str, next_name: str) -> str:
    assert name in source
    assert next_name in source
    start = source.index(name)
    end = source.index(next_name, start + len(name))
    assert start < end
    return source[start:end]


def test_asr_task_has_room_for_pcm_encoding_and_http_tls_call_chain():
    source = RUNTIME.read_text(encoding="utf-8")
    match = re.search(r"#define AIQA_TASK_STACK_ASR\s+(\d+)", source)

    assert match is not None
    assert int(match.group(1)) >= 16384


def test_asr_pcm_encoding_workspace_is_allocated_off_the_task_stack():
    source = ASR_CLIENT.read_text(encoding="utf-8")
    function = source.split(
        "esp_err_t aiqa_asr_transcribe_pcm_wav_once", maxsplit=1
    )[1]

    assert "aiqa_asr_pcm_workspace_t" in source
    assert "calloc(1, sizeof(*workspace))" in function
    assert "uint8_t raw_chunk[AIQA_ASR_BASE64_RAW_CHUNK_BYTES]" not in function
    assert "unsigned char encoded_chunk[AIQA_ASR_BASE64_ENCODED_CHUNK_BYTES]" not in function


def test_new_interaction_releases_queued_asr_pcm_without_resetting_owned_items():
    source = RUNTIME.read_text(encoding="utf-8")
    cleanup = _function_region(
        source,
        "static void clear_pending_asr_commands",
        "static uint32_t begin_new_interaction",
    )
    begin = _function_region(
        source,
        "static uint32_t begin_new_interaction",
        "static esp_err_t init_nvs",
    )

    assert "xQueueReceive(s_asr_queue, &pending, 0)" in cleanup
    assert "clear_asr_command_pcm(&pending);" in cleanup
    assert "clear_pending_asr_commands();" in begin
    assert "xQueueReset(s_asr_queue)" not in source


def test_asr_pcm_cleanup_frees_owned_memory_even_when_capacity_is_invalid():
    source = RUNTIME.read_text(encoding="utf-8")
    cleanup = _function_region(
        source,
        "static void clear_asr_command_pcm",
        "static void clear_pending_asr_commands",
    )

    assert "command->pcm == NULL" in cleanup
    assert "command->pcm_capacity == 0" not in cleanup.split("return;", maxsplit=1)[0]
    assert "if (command->pcm_capacity > 0)" in cleanup
    assert "heap_caps_free((void *)command->pcm);" in cleanup


def test_asr_checks_internal_heap_before_starting_http_tls():
    source = RUNTIME.read_text(encoding="utf-8")
    task = _function_region(source, "static void asr_task", "static void chat_task")

    heap_gate = task.index("aiqa_hardening_heap_allows_model_request")
    transcribe = task.index("aiqa_asr_transcribe_pcm_wav_once")
    assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in task
    assert "heap_caps_get_largest_free_block" in task
    assert "AIQA_ASR_MIN_INTERNAL_LARGEST_BLOCK_BYTES" in task
    assert heap_gate < transcribe
