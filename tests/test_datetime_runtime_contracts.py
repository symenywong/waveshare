from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "components/app_runtime/src/aiqa_runtime.c").read_text(encoding="utf-8")
NET_CONNECT = (ROOT / "components/net_connect/src/aiqa_net_connect.c").read_text(
    encoding="utf-8"
)


def _function_region(source: str, name: str, next_name: str) -> str:
    start = source.index(name)
    end = source.index(next_name, start + len(name))
    return source[start:end]


def test_network_configures_shanghai_timezone_before_accepting_system_time():
    sync = _function_region(
        NET_CONNECT,
        "static esp_err_t sync_time_with_policy",
        "esp_err_t aiqa_net_connect_wifi",
    )

    assert 'setenv("TZ", AIQA_NET_DEFAULT_TIMEZONE, 1)' in sync
    assert "tzset();" in sync
    assert sync.index('setenv("TZ", AIQA_NET_DEFAULT_TIMEZONE, 1)') < sync.index(
        "time(&now)"
    )
    assert "System time already valid" not in sync
    assert "s_time_synchronized" in sync


def test_datetime_local_reply_uses_spoken_path_and_skips_qwen():
    handler = _function_region(
        RUNTIME,
        "static bool handle_local_command_transcript",
        "esp_err_t aiqa_runtime_post_event",
    )
    datetime_branch = handler.split(
        "command.type == AIQA_LOCAL_COMMAND_DATE_QUERY", maxsplit=1
    )[1]

    assert "AIQA_LOCAL_COMMAND_DATETIME_QUERY" in datetime_branch
    assert "aiqa_datetime_format_local_reply" in datetime_branch
    assert "set_local_spoken_reply" in datetime_branch
    assert "set_local_reply(reply)" not in datetime_branch


def test_runtime_injects_only_synchronized_trusted_datetime_into_chat():
    chat_task = _function_region(RUNTIME, "static void chat_task", "enum {")

    assert "current_local_time_ready" in chat_task
    assert "local_time_ready" in chat_task
    assert chat_task.count("current_local_time_ready ? &current_local_time : NULL") >= 2


def test_runtime_requires_this_boot_sntp_sync_before_trusting_clock():
    ready = _function_region(
        RUNTIME,
        "static bool local_time_ready",
        "static void set_local_reply_mode",
    )

    assert "aiqa_net_time_is_synchronized()" in ready
    assert "aiqa_net_time_is_valid(now)" in ready
