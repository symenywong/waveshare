import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.mbedtls_host import build_vendored_mbedcrypto


ROOT = Path(__file__).resolve().parents[1]
IDF_PATH = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf"))


@unittest.skipUnless((IDF_PATH / "components/json/cJSON/cJSON.c").exists(), "ESP-IDF cJSON unavailable")
class PairingRpcTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        temp = Path(cls._tmp.name)
        include, crypto = build_vendored_mbedcrypto(temp / "mbedtls")
        cls.binary = temp / "pairing_rpc_harness"
        sources = [
            ROOT / "tests/c/pairing_rpc_harness.c",
            ROOT / "components/management_access/src/aiqa_management_access.c",
            ROOT / "components/management_session/src/aiqa_pairing_client_session.c",
            ROOT / "components/management_session/src/aiqa_pairing_device_session.c",
            ROOT / "components/management_session/src/aiqa_pairing_crypto.c",
            ROOT / "components/management_session/src/aiqa_pairing_lifecycle.c",
            ROOT / "components/management_session/src/aiqa_secure_channel.c",
            ROOT / "components/management_transport/src/aiqa_pairing_rpc.c",
            IDF_PATH / "components/json/cJSON/cJSON.c",
        ]
        subprocess.run(
            [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-deprecated-declarations", "-fsanitize=address,undefined",
                f"-I{ROOT / 'components/management_access/include'}",
                f"-I{ROOT / 'components/management_session/include'}",
                f"-I{ROOT / 'components/management_transport/include'}",
                f"-I{include}", f"-I{IDF_PATH / 'components/json/cJSON'}",
                *(str(source) for source in sources), str(crypto), "-lm",
                "-o", str(cls.binary),
            ],
            cwd=ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def run_case(self, case: str) -> None:
        subprocess.run([str(self.binary), case], cwd=ROOT, check=True)

    def test_json_rpc_pairing_finished_and_secure_channel_roundtrip(self):
        self.run_case("roundtrip")

    def test_malformed_pairing_requests_fail_closed(self):
        self.run_case("reject")


if __name__ == "__main__":
    unittest.main()
