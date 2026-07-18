import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class ConfigTransactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "config_transaction_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-pthread",
                f"-I{REPO_ROOT / 'components/config_store/include'}",
                f"-I{REPO_ROOT / 'components/provider_common/include'}",
                str(REPO_ROOT / "tests/c/config_transaction_harness.c"),
                str(REPO_ROOT / "components/config_store/src/aiqa_config_transaction.c"),
                str(REPO_ROOT / "components/config_store/src/aiqa_config.c"),
                str(REPO_ROOT / "components/provider_common/src/aiqa_provider.c"),
                "-o",
                str(cls.binary),
            ],
            cwd=REPO_ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def run_case(self, name: str) -> None:
        subprocess.run([str(self.binary), name], cwd=REPO_ROOT, check=True)

    def test_success_is_ordered_and_immutable(self):
        self.run_case("success")

    def test_invalid_or_stale_requests_do_not_call_ports(self):
        self.run_case("validation")

    def test_stage_and_verify_failures_do_not_touch_network(self):
        self.run_case("storage_failures")

    def test_trial_failure_restores_the_previous_network(self):
        self.run_case("trial_failure")

    def test_recovery_failure_is_reported_separately(self):
        self.run_case("recovery_failure")

    def test_activation_failure_restores_network_and_snapshot(self):
        self.run_case("activation_failure")

    def test_activation_recovery_failure_is_reported_separately(self):
        self.run_case("activation_recovery_failure")

    def test_reentrant_updates_are_rejected_as_busy(self):
        self.run_case("reentrant")

    def test_parallel_updates_are_serialized(self):
        self.run_case("parallel")

    def test_indeterminate_activation_locks_the_transaction_for_recovery(self):
        self.run_case("activation_indeterminate")

    def test_candidate_cleanup_failure_is_reported_and_latched(self):
        self.run_case("cleanup_failure")

    def test_retired_slot_cleanup_failure_is_reported_and_latched(self):
        self.run_case("retired_slot_cleanup_failure")

    def test_public_api_rejects_invalid_state_and_names_all_statuses(self):
        self.run_case("api_edges")

    def test_nvs_retired_slot_cleanup_commits_and_migration_erases_legacy(self):
        source = (
            REPO_ROOT / "components/config_store/src/aiqa_config_nvs_ab.c"
        ).read_text()

        migration = source[
            source.index("if (activation == AIQA_CONFIG_ACTIVATION_COMMITTED)") :
            source.index("aiqa_config_record_secure_clear(&migrated);")
        ]
        normalized_migration = " ".join(migration.split())
        self.assertIn(
            "aiqa_config_nvs_discard_record( AIQA_CONFIG_SLOT_LEGACY)",
            normalized_migration,
        )
        self.assertGreaterEqual(
            source.count("aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_LEGACY)"),
            1,
        )
        self.assertGreaterEqual(source.count("AIQA_CONFIG_SLOT_LEGACY))"), 2)
        discard = source[source.index("bool aiqa_config_nvs_discard_record") :]
        self.assertIn("ret = nvs_commit(handle);", discard)
        self.assertNotIn("(void)nvs_commit(handle);", discard)
        legacy_source = (
            REPO_ROOT / "components/config_store/src/aiqa_config_nvs.c"
        ).read_text()
        legacy_cleanup = legacy_source[
            legacy_source.index("aiqa_config_erase_legacy_record_from_nvs") :
            legacy_source.index("aiqa_config_load_legacy_prefs_from_nvs")
        ]
        for key in ("wifi_ssid", "wifi_pass", "chat_key", "asr_key"):
            self.assertIn(f'"{key}"', legacy_cleanup)
        self.assertIn("nvs_commit(handle)", legacy_cleanup)
        self.assertIn("nvs_get_str(handle, secret_keys[index]", legacy_cleanup)
        self.assertIn("if (ret == ESP_OK && changed)", legacy_cleanup)


if __name__ == "__main__":
    unittest.main()
