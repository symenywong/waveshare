from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/app_runtime/src/aiqa_runtime.c"
RUNTIME_HEADER = ROOT / "components/app_runtime/include/aiqa_runtime.h"


def test_management_wifi_secrets_are_not_copied_into_freertos_queue_storage():
    source = RUNTIME.read_text(encoding="utf-8")

    assert "aiqa_management_wifi_job_t *wifi_update_job;" in source
    assert "aiqa_wifi_connect_job_t *connect_job;" in source
    assert "aiqa_wifi_credentials_t credentials;" not in source.split(
        "} aiqa_net_command_t;", maxsplit=1
    )[0].split("typedef struct {", maxsplit=-1)[-1]


def test_management_wifi_jobs_are_cleared_on_completion_and_queue_drain():
    source = RUNTIME.read_text(encoding="utf-8")

    assert "release_management_wifi_job(command.wifi_update_job);" in source
    assert "release_management_wifi_job(pending.wifi_update_job);" in source
    assert "aiqa_management_owned_wifi_update_secure_clear(&job->update);" in source
    assert "heap_caps_free(job);" in source


def test_runtime_exposes_transport_agnostic_management_entry_points():
    header = RUNTIME_HEADER.read_text(encoding="utf-8")

    assert "aiqa_runtime_management_get_status" in header
    assert "aiqa_runtime_management_get_public_config" in header
    assert "aiqa_runtime_management_submit_wifi_update" in header


def test_management_updates_run_through_atomic_config_transaction():
    source = RUNTIME.read_text(encoding="utf-8")

    assert "aiqa_config_transaction_apply_wifi" in source
    assert "aiqa_config_nvs_storage_ports" in source
    assert "aiqa_net_transaction_ports" in source


def test_factory_reset_is_serialized_by_the_network_config_owner():
    source = RUNTIME.read_text(encoding="utf-8")
    app_state_task = source.split("static void app_state_task", maxsplit=1)[1].split(
        "static void ui_task", maxsplit=1
    )[0]
    net_task = source.split("static void net_task", maxsplit=1)[1].split(
        "static void asr_task", maxsplit=1
    )[0]

    assert "AIQA_NET_COMMAND_FACTORY_RESET" in app_state_task
    assert "aiqa_config_erase_nvs_namespace" not in app_state_task
    assert "AIQA_NET_COMMAND_FACTORY_RESET" in net_task
    assert "aiqa_config_erase_nvs_namespace" in net_task
    assert "aiqa_net_forget_wifi" in net_task
