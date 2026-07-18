import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IDF_PATH = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf"))


@unittest.skipUnless((IDF_PATH / "components/json/cJSON/cJSON.c").exists(), "ESP-IDF cJSON unavailable")
class AuthenticatedManagementProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "management_authenticated_protocol_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-deprecated-declarations",
                "-fsanitize=address,undefined",
                f"-I{ROOT / 'tests/fakes'}",
                f"-I{ROOT / 'components/management_access/include'}",
                f"-I{ROOT / 'components/management_transport/include'}",
                f"-I{ROOT / 'components/management_service/include'}",
                f"-I{ROOT / 'components/config_store/include'}",
                f"-I{ROOT / 'components/app_core/include'}",
                f"-I{IDF_PATH / 'components/json/cJSON'}",
                str(ROOT / "tests/c/management_authenticated_protocol_harness.c"),
                str(ROOT / "components/management_transport/src/aiqa_management_protocol.c"),
                str(IDF_PATH / "components/json/cJSON/cJSON.c"),
                "-lm",
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

    def test_status_and_public_configuration_are_serialized_from_ports(self):
        self.run_case("reads")

    def test_wifi_update_is_strict_and_returns_an_operation_id(self):
        self.run_case("wifi")

    def test_unknown_or_unexpected_authenticated_requests_fail_closed(self):
        self.run_case("reject")


if __name__ == "__main__":
    unittest.main()
