import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IDF_PATH = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf"))


@unittest.skipUnless((IDF_PATH / "components/json/cJSON/cJSON.c").exists(), "ESP-IDF cJSON unavailable")
class ManagementProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "management_protocol_harness"
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
                f"-I{ROOT / 'components/management_service/include'}",
                f"-I{ROOT / 'components/config_store/include'}",
                f"-I{ROOT / 'components/app_core/include'}",
                f"-I{ROOT / 'components/management_transport/include'}",
                f"-I{IDF_PATH / 'components/json/cJSON'}",
                str(ROOT / "tests/c/management_protocol_harness.c"),
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

    def test_public_hello_reports_version_and_authentication_gate(self):
        self.run_case("hello")

    def test_public_hello_diagnostics_is_fixed_schema_parameterless_and_content_free(self):
        self.run_case("diagnostics")

    def test_unpaired_methods_and_unknown_fields_fail_closed(self):
        self.run_case("reject")

    def test_embedded_nul_cannot_truncate_the_validated_payload(self):
        self.run_case("nul")

    def test_buffer_and_argument_boundaries_fail_safely(self):
        self.run_case("boundaries")

    def test_deep_json_is_rejected_before_recursive_parsing(self):
        self.run_case("depth")


if __name__ == "__main__":
    unittest.main()
