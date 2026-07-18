import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class ManagementServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "management_service_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-pthread",
                f"-I{REPO_ROOT / 'components/management_access/include'}",
                f"-I{REPO_ROOT / 'components/management_service/include'}",
                f"-I{REPO_ROOT / 'components/config_store/include'}",
                f"-I{REPO_ROOT / 'components/app_core/include'}",
                f"-I{REPO_ROOT / 'components/provider_common/include'}",
                f"-I{REPO_ROOT / 'tests/fakes'}",
                str(REPO_ROOT / "tests/c/management_service_harness.c"),
                str(REPO_ROOT / "components/management_service/src/aiqa_management_service.c"),
                str(REPO_ROOT / "components/config_store/src/aiqa_config.c"),
                str(REPO_ROOT / "components/app_core/src/aiqa_assistant_profile.c"),
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

    def test_reads_status_and_allowlisted_public_config(self):
        self.run_case("read")

    def test_requires_trusted_read_and_write_capabilities(self):
        self.run_case("security")

    def test_serializes_wifi_operations_and_publishes_completion(self):
        self.run_case("submit")

    def test_rejects_invalid_wifi_updates_before_queueing(self):
        self.run_case("validation")

    def test_public_projection_never_contains_secret_values(self):
        self.run_case("projection")

    def test_completion_cannot_be_blocked_by_a_slow_status_reader(self):
        self.run_case("concurrency")

    def test_invalid_ports_and_unknown_statuses_fail_closed(self):
        self.run_case("edges")


if __name__ == "__main__":
    unittest.main()
