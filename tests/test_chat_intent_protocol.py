import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IDF_PATH = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf"))


@unittest.skipUnless((IDF_PATH / "components/json/cJSON/cJSON.c").exists(), "ESP-IDF cJSON unavailable")
class ChatIntentProtocolTests(unittest.TestCase):
    def test_request_and_response_contract_is_strict(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "chat_intent_protocol_harness"
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
                    f"-I{ROOT / 'components/chat_client/include'}",
                    f"-I{ROOT / 'components/config_store/include'}",
                    f"-I{ROOT / 'components/provider_common/include'}",
                    f"-I{ROOT / 'components/app_core/include'}",
                    f"-I{ROOT / 'components/app_runtime/include'}",
                    f"-I{IDF_PATH / 'components/json/cJSON'}",
                    str(ROOT / "tests/c/chat_intent_protocol_harness.c"),
                    str(ROOT / "components/chat_client/src/aiqa_chat_intent_protocol.c"),
                    str(ROOT / "components/app_runtime/src/aiqa_device_intent_controller.c"),
                    str(ROOT / "components/app_core/src/aiqa_device_intent.c"),
                    str(ROOT / "components/app_core/src/aiqa_assistant_profile.c"),
                    str(ROOT / "components/app_core/src/aiqa_language.c"),
                    str(ROOT / "components/config_store/src/aiqa_config.c"),
                    str(ROOT / "components/provider_common/src/aiqa_provider.c"),
                    str(IDF_PATH / "components/json/cJSON/cJSON.c"),
                    "-lm",
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
