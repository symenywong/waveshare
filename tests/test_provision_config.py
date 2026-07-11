import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "tools" / "provision_config.py"


class ProvisionConfigTests(unittest.TestCase):
    def test_rejects_api_key_command_line_argument(self):
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--provider",
                "dashscope_openai_chat",
                "--api-key",
                "sk-should-not-be-accepted",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertNotEqual(result.returncode, 0)
        combined_output = result.stdout + result.stderr
        self.assertIn("API keys must be provided through stdin or environment variables", combined_output)
        self.assertNotIn("sk-should-not-be-accepted", combined_output)

    def test_dry_run_redacts_key_from_environment(self):
        env = {
            **dict(),
            "AIQA_API_KEY": "sk-1234567890abcdef",
        }
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--provider",
                "dashscope_openai_chat",
                "--base-url",
                "https://dashscope.aliyuncs.com/compatible-mode/v1",
                "--model",
                "qwen3.7-max",
                "--dry-run",
            ],
            cwd=REPO_ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("sk-***cdef", result.stdout)
        self.assertIn("dashscope_qwen_asr_flash", result.stdout)
        self.assertNotIn("sk-1234567890abcdef", result.stdout + result.stderr)

    def test_nvs_csv_contains_real_key_but_stdout_stays_redacted(self):
        with tempfile.TemporaryDirectory() as tmp:
            nvs_csv = Path(tmp) / "aiqa.secrets.csv"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--provider",
                    "dashscope_openai_chat",
                    "--base-url",
                    "https://dashscope.aliyuncs.com/compatible-mode/v1",
                    "--model",
                    "qwen3.7-max",
                    "--wifi-ssid",
                    "lab-wifi",
                    "--wifi-password",
                    "correct-horse",
                    "--nvs-csv",
                    str(nvs_csv),
                    "--allow-plaintext-nvs",
                ],
                cwd=REPO_ROOT,
                input="sk-1234567890abcdef\n",
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("sk-***cdef", result.stdout)
            self.assertNotIn("sk-1234567890abcdef", result.stdout + result.stderr)

            csv_text = nvs_csv.read_text(encoding="utf-8")
            self.assertIn("aiqa,namespace,,", csv_text)
            self.assertIn("chat_key,data,string,sk-1234567890abcdef", csv_text)
            self.assertIn("asr_key,data,string,sk-1234567890abcdef", csv_text)
            self.assertIn("asr_provider,data,string,dashscope_qwen_asr_flash", csv_text)
            self.assertIn("asr_model,data,string,qwen3-asr-flash", csv_text)
            self.assertIn("asr_base_url,data,string,https://dashscope.aliyuncs.com/compatible-mode/v1", csv_text)
            self.assertIn("wifi_ssid,data,string,lab-wifi", csv_text)
            self.assertEqual(nvs_csv.stat().st_mode & 0o777, 0o600)

    def test_minimax_chat_can_keep_default_qwen_asr(self):
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--provider",
                "minimax_openai_chat",
                "--base-url",
                "https://api.minimax.io/v1",
                "--model",
                "MiniMax-M3",
                "--dry-run",
            ],
            cwd=REPO_ROOT,
            env={"AIQA_API_KEY": "sk-1234567890abcdef"},
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"provider": "minimax_openai_chat"', result.stdout)
        self.assertIn('"provider": "dashscope_qwen_asr_flash"', result.stdout)
        self.assertNotIn("sk-1234567890abcdef", result.stdout + result.stderr)

    def test_refuses_plaintext_nvs_without_explicit_opt_in(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--provider",
                    "dashscope_openai_chat",
                    "--base-url",
                    "https://dashscope.aliyuncs.com/compatible-mode/v1",
                    "--model",
                    "qwen3.7-max",
                    "--wifi-ssid",
                    "lab-wifi",
                    "--wifi-password",
                    "correct-horse",
                    "--nvs-csv",
                    str(Path(tmp) / "aiqa.secrets.csv"),
                ],
                cwd=REPO_ROOT,
                input="sk-1234567890abcdef\n",
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--allow-plaintext-nvs", result.stderr)
            self.assertNotIn("sk-1234567890abcdef", result.stdout + result.stderr)

    def test_refuses_to_overwrite_existing_secret_csv(self):
        with tempfile.TemporaryDirectory() as tmp:
            nvs_csv = Path(tmp) / "aiqa.secrets.csv"
            nvs_csv.write_text("existing\n", encoding="utf-8")
            nvs_csv.chmod(0o644)

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--provider",
                    "dashscope_openai_chat",
                    "--base-url",
                    "https://dashscope.aliyuncs.com/compatible-mode/v1",
                    "--model",
                    "qwen3.7-max",
                    "--wifi-ssid",
                    "lab-wifi",
                    "--wifi-password",
                    "correct-horse",
                    "--nvs-csv",
                    str(nvs_csv),
                    "--allow-plaintext-nvs",
                ],
                cwd=REPO_ROOT,
                input="sk-1234567890abcdef\n",
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("already exists", result.stderr)
            self.assertEqual(nvs_csv.read_text(encoding="utf-8"), "existing\n")


if __name__ == "__main__":
    unittest.main()
