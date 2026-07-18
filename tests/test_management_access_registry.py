import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ManagementAccessRegistryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "management_access_registry_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                f"-I{ROOT / 'tests/fakes'}",
                f"-I{ROOT / 'components/management_access/include'}",
                f"-I{ROOT / 'components/management_service/include'}",
                f"-I{ROOT / 'components/config_store/include'}",
                f"-I{ROOT / 'components/app_core/include'}",
                str(ROOT / "tests/c/management_access_registry_harness.c"),
                str(ROOT / "components/management_access/src/aiqa_management_access.c"),
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

    def test_prepared_access_is_not_authorized_until_activation(self):
        self.run_case("activation")

    def test_every_access_dimension_must_match(self):
        self.run_case("mismatch")

    def test_revoke_invalidates_old_access_before_reuse(self):
        self.run_case("revoke")


if __name__ == "__main__":
    unittest.main()
