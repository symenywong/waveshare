import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PairingLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "pairing_lifecycle_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                f"-I{ROOT / 'components/management_session/include'}",
                str(ROOT / "tests/c/pairing_lifecycle_harness.c"),
                str(ROOT / "components/management_session/src/aiqa_pairing_lifecycle.c"),
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

    def test_local_presence_uniform_code_and_exact_window_expiry(self):
        self.run_case("open-expiry")

    def test_attempt_is_reserved_before_crypto_and_lock_survives_reboot(self):
        self.run_case("attempt-lock")

    def test_finished_send_boundary_and_session_idle_expiry(self):
        self.run_case("success-session")

    def test_rng_display_storage_and_clock_fail_closed(self):
        self.run_case("faults")

    def test_single_handshake_step_timeout_and_disconnect_do_not_refund(self):
        self.run_case("single-handshake")

    def test_api_state_persistence_and_session_boundaries(self):
        self.run_case("boundaries")

    def test_pairing_window_is_bound_to_the_local_usb_connection(self):
        self.run_case("connection-binding")

    def test_progress_and_activity_cannot_extend_absolute_deadlines(self):
        self.run_case("temporal-ceilings")

    def test_clear_failure_revokes_and_faults_without_refunding(self):
        self.run_case("clear-failure")


if __name__ == "__main__":
    unittest.main()
