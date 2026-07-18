import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BootGestureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "boot_gesture_harness"
        subprocess.run(
            [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                f"-I{ROOT / 'components/app_core/include'}",
                str(ROOT / "tests/c/boot_gesture_harness.c"),
                str(ROOT / "components/app_core/src/aiqa_boot_gesture.c"),
                str(ROOT / "components/app_core/src/aiqa_ptt_button.c"),
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

    def test_three_taps_open_pairing_only_after_disambiguation_gap(self):
        self.run_case("pairing")

    def test_five_taps_request_explicit_local_lockout_reset(self):
        self.run_case("reset")

    def test_long_press_remains_ptt_and_cancels_pending_taps(self):
        self.run_case("long")

    def test_ptt_elapsed_time_handles_uint32_wrap(self):
        self.run_case("wrap")


if __name__ == "__main__":
    unittest.main()
