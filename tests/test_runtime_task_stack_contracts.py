import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RuntimeTaskStackContractTests(unittest.TestCase):
    def test_state_task_has_room_for_config_load_call_chain(self):
        runtime = (
            ROOT / "components/app_runtime/src/aiqa_runtime.c"
        ).read_text()
        match = re.search(r"#define AIQA_TASK_STACK_APP\s+(\d+)", runtime)

        self.assertIsNotNone(match)
        self.assertGreaterEqual(
            int(match.group(1)),
            12288,
            "aiqa_state overflowed at boot while load_config_event was active",
        )


if __name__ == "__main__":
    unittest.main()
