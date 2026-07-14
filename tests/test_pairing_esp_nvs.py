import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PairingEspNvsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "pairing_esp_nvs_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                f"-I{ROOT / 'tests/fakes'}",
                f"-I{ROOT / 'components/management_session/include'}",
                f"-I{ROOT / 'components/management_session_esp/include'}",
                str(ROOT / "tests/c/pairing_esp_nvs_harness.c"),
                str(ROOT / "tests/fakes/fake_nvs.c"),
                str(
                    ROOT
                    / "components/management_session_esp/src/aiqa_pairing_esp_nvs.c"
                ),
                "-o",
                str(cls.binary),
            ],
            cwd=ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def run_case(self, case: str) -> None:
        subprocess.run([str(self.binary), case], cwd=ROOT, check=True)

    def test_missing_record_and_durable_roundtrip(self):
        self.run_case("missing-roundtrip")

    def test_commit_errors_fail_closed_without_refunding_durable_writes(self):
        self.run_case("commit-reconciliation")

    def test_corrupt_and_invalid_records_fail_closed(self):
        self.run_case("corrupt-invalid")


if __name__ == "__main__":
    unittest.main()
