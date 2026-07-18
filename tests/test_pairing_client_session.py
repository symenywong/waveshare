import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.mbedtls_host import build_vendored_mbedcrypto


ROOT = Path(__file__).resolve().parents[1]


class PairingClientSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        temp = Path(cls._tmp.name)
        include, crypto = build_vendored_mbedcrypto(temp / "mbedtls")
        cls.binary = temp / "pairing_client_session_harness"
        subprocess.run(
            [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-deprecated-declarations", "-fsanitize=address,undefined",
                f"-I{ROOT / 'components/management_session/include'}",
                f"-I{include}",
                str(ROOT / "tests/c/pairing_client_session_harness.c"),
                str(ROOT / "components/management_session/src/aiqa_pairing_client_session.c"),
                str(ROOT / "components/management_session/src/aiqa_pairing_crypto.c"),
                str(ROOT / "components/management_session/src/aiqa_secure_channel.c"),
                str(crypto), "-o", str(cls.binary),
            ],
            cwd=ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def run_case(self, case: str) -> None:
        subprocess.run([str(self.binary), case], cwd=ROOT, check=True)

    def test_opaque_client_completes_finished_and_secure_roundtrip(self):
        self.run_case("roundtrip")

    def test_client_state_machine_rejects_early_operations(self):
        self.run_case("state")


if __name__ == "__main__":
    unittest.main()
