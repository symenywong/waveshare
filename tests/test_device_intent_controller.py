import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DeviceIntentControllerTests(unittest.TestCase):
    def test_proposals_require_one_fresh_confirmation_before_persisting(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "device_intent_controller_harness"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    f"-I{ROOT / 'tests/fakes'}",
                    f"-I{ROOT / 'components/app_runtime/include'}",
                    f"-I{ROOT / 'components/app_core/include'}",
                    str(ROOT / "tests/c/device_intent_controller_harness.c"),
                    str(ROOT / "components/app_runtime/src/aiqa_device_intent_controller.c"),
                    str(ROOT / "components/app_core/src/aiqa_device_intent.c"),
                    str(ROOT / "components/app_core/src/aiqa_assistant_profile.c"),
                    str(ROOT / "components/app_core/src/aiqa_language.c"),
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
