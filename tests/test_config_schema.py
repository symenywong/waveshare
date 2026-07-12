import unittest

from tools.config_schema import (
    ConfigValidationError,
    ProviderCatalog,
    redact_secret,
    validate_provider_config,
)


class ConfigSchemaTests(unittest.TestCase):
    def test_redacts_secret_without_leaking_middle(self):
        self.assertEqual(redact_secret("sk-1234567890abcdef"), "sk-***cdef")
        self.assertEqual(redact_secret("short"), "***")
        self.assertEqual(redact_secret(""), "***")

    def test_rejects_unapproved_provider_host(self):
        catalog = ProviderCatalog.default()

        with self.assertRaises(ConfigValidationError):
            validate_provider_config(
                catalog,
                {
                    "provider": "dashscope_openai_chat",
                    "base_url": "https://evil.example.com/compatible-mode/v1",
                    "model": "qwen3.7-max",
                    "api_key": "sk-test",
                },
            )

    def test_accepts_dashscope_qwen37max_configuration(self):
        catalog = ProviderCatalog.default()
        config = validate_provider_config(
            catalog,
            {
                "provider": "dashscope_openai_chat",
                "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
                "model": "qwen3.7-max",
                "api_key": "sk-test",
            },
        )

        self.assertEqual(config["provider"], "dashscope_openai_chat")
        self.assertEqual(config["model"], "qwen3.7-max")
        self.assertTrue(config["capabilities"]["supports_chat_stream"])

    def test_accepts_dashscope_qwen_tts_configuration(self):
        catalog = ProviderCatalog.default()
        config = validate_provider_config(
            catalog,
            {
                "provider": "dashscope_qwen_tts",
                "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
                "model": "qwen-tts",
                "api_key": "sk-test",
            },
        )

        self.assertEqual(config["provider"], "dashscope_qwen_tts")
        self.assertTrue(config["capabilities"]["supports_tts_stream"])

    def test_rejects_display_name_for_qwen_model(self):
        catalog = ProviderCatalog.default()

        with self.assertRaises(ConfigValidationError):
            validate_provider_config(
                catalog,
                {
                    "provider": "dashscope_openai_chat",
                    "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
                    "model": "qwen 3.7 max",
                    "api_key": "sk-test",
                },
            )

    def test_maas_workspace_host_is_only_allowed_for_dashscope(self):
        catalog = ProviderCatalog.default()

        dashscope_config = validate_provider_config(
            catalog,
            {
                "provider": "dashscope_openai_chat",
                "base_url": "https://workspace-id.ap-southeast-1.maas.aliyuncs.com/compatible-mode/v1",
                "model": "qwen3.7-max",
                "api_key": "sk-test",
            },
        )
        self.assertEqual(dashscope_config["provider"], "dashscope_openai_chat")

        with self.assertRaises(ConfigValidationError):
            validate_provider_config(
                catalog,
                {
                    "provider": "minimax_openai_chat",
                    "base_url": "https://workspace-id.ap-southeast-1.maas.aliyuncs.com/v1",
                    "model": "MiniMax-M3",
                    "api_key": "sk-test",
                },
            )

    def test_asr_provider_accepts_dashscope_maas_hosts_like_device_firmware(self):
        catalog = ProviderCatalog.default()
        config = validate_provider_config(
            catalog,
            {
                "provider": "dashscope_qwen_asr_flash",
                "base_url": "https://workspace-id.ap-southeast-1.maas.aliyuncs.com/compatible-mode/v1",
                "model": "qwen3-asr-flash",
                "api_key": "sk-test",
            },
        )

        self.assertEqual(config["provider"], "dashscope_qwen_asr_flash")

    def test_rejects_values_that_do_not_fit_device_buffers(self):
        catalog = ProviderCatalog.default()

        with self.assertRaises(ConfigValidationError):
            validate_provider_config(
                catalog,
                {
                    "provider": "dashscope_openai_chat",
                    "base_url": "https://dashscope.aliyuncs.com/" + ("x" * 180),
                    "model": "qwen3.7-max",
                    "api_key": "sk-test",
                },
            )

        with self.assertRaises(ConfigValidationError):
            validate_provider_config(
                catalog,
                {
                    "provider": "dashscope_openai_chat",
                    "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
                    "model": "qwen3.7-max",
                    "api_key": "sk-" + ("x" * 220),
                },
            )


if __name__ == "__main__":
    unittest.main()
