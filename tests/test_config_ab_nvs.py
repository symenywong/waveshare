import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class ConfigAbNvsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "config_ab_nvs_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                f"-I{REPO_ROOT / 'tests/fakes'}",
                f"-I{REPO_ROOT / 'components/config_store/include'}",
                f"-I{REPO_ROOT / 'components/config_store/src'}",
                f"-I{REPO_ROOT / 'components/app_core/include'}",
                f"-I{REPO_ROOT / 'components/provider_common/include'}",
                str(REPO_ROOT / "tests/c/config_ab_nvs_harness.c"),
                str(REPO_ROOT / "tests/fakes/fake_nvs.c"),
                str(REPO_ROOT / "components/config_store/src/aiqa_config_nvs.c"),
                str(REPO_ROOT / "components/config_store/src/aiqa_config_nvs_ab.c"),
                str(REPO_ROOT / "components/config_store/src/aiqa_config.c"),
                str(REPO_ROOT / "components/config_store/src/aiqa_config_transaction.c"),
                str(REPO_ROOT / "components/app_core/src/aiqa_assistant_profile.c"),
                str(REPO_ROOT / "components/app_core/src/aiqa_language.c"),
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

    def run_case(self, case: str) -> None:
        subprocess.run([str(self.binary), case], cwd=REPO_ROOT, check=True)

    def test_legacy_is_loaded_only_when_head_is_missing(self):
        self.run_case("legacy")

    def test_a_and_b_records_activate_and_survive_restart(self):
        self.run_case("ab_cycle")

    def test_orphan_candidate_never_overrides_the_active_head(self):
        self.run_case("orphan")

    def test_stage_commit_errors_are_reconciled_by_readback(self):
        self.run_case("stage_commit")

    def test_head_commit_errors_are_classified_by_durable_readback(self):
        self.run_case("head_commit")

    def test_unreadable_head_after_commit_is_indeterminate(self):
        self.run_case("head_indeterminate")

    def test_discard_and_factory_reset_remove_all_config_namespaces(self):
        self.run_case("reset")

    def test_each_record_field_failure_is_rejected(self):
        self.run_case("field_failures")

    def test_invalid_records_heads_and_api_arguments_fail_closed(self):
        self.run_case("api_edges")

    def test_storage_port_adapter_uses_the_ab_backend(self):
        self.run_case("storage_ports")

    def test_reset_tombstone_hides_remaining_legacy_credentials(self):
        self.run_case("reset_tombstone")

    def test_missing_head_never_resurrects_migrated_legacy_credentials(self):
        self.run_case("missing_head_after_migration")

    def test_failed_migration_activation_discards_candidate_and_retries(self):
        self.run_case("migration_activation_not_committed")

    def test_failed_legacy_cleanup_fails_closed_and_retries_after_restart(self):
        self.run_case("migration_cleanup_retry")

    def test_user_preferences_survive_restart_and_failed_commits_do_not_replace_them(self):
        self.run_case("user_prefs_round_trip")


if __name__ == "__main__":
    unittest.main()
