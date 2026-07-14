import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.mbedtls_host import build_vendored_mbedcrypto


ROOT = Path(__file__).resolve().parents[1]


class PairingCryptoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        temp = Path(cls._tmp.name)
        include, library = build_vendored_mbedcrypto(temp / "mbedtls")
        cls.binary = temp / "pairing_crypto_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                f"-I{ROOT / 'components/management_session/include'}",
                f"-I{include}",
                str(ROOT / "tests/c/pairing_crypto_harness.c"),
                str(ROOT / "components/management_session/src/aiqa_pairing_crypto.c"),
                str(ROOT / "components/management_session/src/aiqa_secure_channel.c"),
                str(library),
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

    def test_matching_codes_derive_identical_keys_and_confirm_both_roles(self):
        self.run_case("roundtrip")

    def test_wrong_code_fails_key_confirmation(self):
        self.run_case("wrong-code")

    def test_device_identity_is_bound_to_finished_confirmation(self):
        self.run_case("transcript-binding")

    def test_out_of_order_and_short_buffer_calls_fail_closed(self):
        self.run_case("state")

    def test_rng_failure_zeroes_output_and_permanently_fails_context(self):
        self.run_case("failure-paths")

    def test_tampered_ecjpake_round_permanently_fails_context(self):
        self.run_case("tampered-round")

    def test_aead_rejects_replay_and_does_not_advance_after_tampering(self):
        self.run_case("channel")


if __name__ == "__main__":
    unittest.main()
