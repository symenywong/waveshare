from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_tts_playback_queue_keeps_large_pcm_off_task_stack():
    source = (ROOT / "components/app_runtime/src/aiqa_runtime.c").read_text()

    assert "uint8_t pcm[AIQA_AUDIO_PLAYBACK_CHUNK_BYTES];" in source
    assert "#define AIQA_TTS_PLAYBACK_SLOT_COUNT 96" in source
    assert "AIQA_TTS_PLAYBACK_PLAY_QUEUE_DEPTH (AIQA_TTS_PLAYBACK_SLOT_COUNT + 1u)" in source
    assert "#define AIQA_TTS_PLAYBACK_INITIAL_BUFFER_MS 500u" in source
    assert "#define AIQA_TTS_PLAYBACK_RESUME_BUFFER_MS 340u" in source
    assert "#define AIQA_TTS_PLAYBACK_PREBUFFER_TIMEOUT_MS 900u" in source
    assert "#define AIQA_TTS_PLAYBACK_STARVATION_THRESHOLD_MS 40u" in source
    assert "atomic_bool producer_done;" in source
    assert "atomic_int status;" in source
    assert "atomic_size_t queued_pcm_bytes;" in source
    assert "atomic_fetch_add_explicit" in source
    assert "atomic_compare_exchange_weak_explicit" in source
    assert "sizeof(aiqa_tts_playback_slot_t *)" in source
    assert "sizeof(aiqa_tts_playback_item_t)" not in source
    assert "aiqa_tts_playback_slot_t slot = {" not in source


def test_tts_stream_clears_only_each_event_payload():
    source = (ROOT / "components/tts_client/src/aiqa_tts_client.c").read_text()
    audio_handler = source.split(
        "static void process_stream_event", maxsplit=1
    )[1].split("static void process_stream_chunk", maxsplit=1)[0]

    assert "const size_t audio_b64_len = strlen(context->audio_b64);" in audio_handler
    assert "context->audio_b64, sizeof(context->audio_b64), audio_b64_len" in audio_handler
    assert "context->pcm, sizeof(context->pcm), pcm_len" in audio_handler
    assert "secure_zero(context->audio_b64, sizeof(context->audio_b64))" not in audio_handler
    assert "secure_zero(context->pcm, sizeof(context->pcm))" not in audio_handler


def test_local_voice_commands_complete_without_online_tts_playback():
    source = (ROOT / "components/app_runtime/src/aiqa_runtime.c").read_text()
    handler = source.split("static void handle_chat_prompt_transition", maxsplit=1)[1].split(
        "static bool handle_language_switch_transcript", maxsplit=1
    )[0]

    assert "s_pending_local_pet_reply_visual_only" in handler
    assert "AIQA_EVENT_CHAT_DONE" in handler
    assert "post_local_pet_reply" not in handler.split(
        "s_pending_local_pet_reply_visual_only", maxsplit=1
    )[1].split("} else {", maxsplit=1)[0]
    assert "post_chat_prompt(s_latest_transcript)" in handler
