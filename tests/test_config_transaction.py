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

    def test_public_api_rejects_invalid_state_and_names_all_statuses(self):
        self.run_case("api_edges")


if __name__ == "__main__":
    unittest.main()
