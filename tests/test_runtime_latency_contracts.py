from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_tts_playback_queue_keeps_large_pcm_off_task_stack():
    source = (ROOT / "components/app_runtime/src/aiqa_runtime.c").read_text()

    assert "uint8_t pcm[AIQA_AUDIO_PLAYBACK_CHUNK_BYTES];" in source
    assert "#define AIQA_TTS_PLAYBACK_SLOT_COUNT 96" in source
    assert "AIQA_TTS_PLAYBACK_PLAY_QUEUE_DEPTH (AIQA_TTS_PLAYBACK_SLOT_COUNT + 1u)" in source
    assert "#define AIQA_TTS_PLAYBACK_PREBUFFER_SLOTS 24" in source
    assert "#define AIQA_TTS_PLAYBACK_PREBUFFER_TIMEOUT_MS 900u" in source
    assert "sizeof(aiqa_tts_playback_slot_t *)" in source
    assert "sizeof(aiqa_tts_playback_item_t)" not in source
    assert "aiqa_tts_playback_slot_t slot = {" not in source
