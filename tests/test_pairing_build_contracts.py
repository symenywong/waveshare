import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PairingBuildContractTests(unittest.TestCase):
    def test_required_mbedtls_primitives_are_enabled_by_default(self):
        defaults = (ROOT / "sdkconfig.defaults").read_text()

        self.assertIn("CONFIG_MBEDTLS_ECJPAKE_C=y", defaults)
        self.assertIn("CONFIG_MBEDTLS_HKDF_C=y", defaults)
        self.assertIn("CONFIG_MBEDTLS_GCM_C=y", defaults)
        self.assertIn("CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y", defaults)

    def test_pairing_component_declares_mbedtls_without_runtime_access(self):
        cmake = (ROOT / "components/management_session/CMakeLists.txt").read_text()

        self.assertIn("REQUIRES mbedtls", cmake)
        self.assertNotIn("app_runtime", cmake)
        self.assertNotIn("management_service", cmake)

    def test_production_pairing_sources_do_not_embed_test_rngs(self):
        sources = "\n".join(
            path.read_text()
            for path in (ROOT / "components/management_session/src").glob("*.c")
        ).lower()

        self.assertNotIn("deterministic_rng", sources)
        self.assertNotIn("fixed_entropy", sources)
        self.assertNotIn("test_seed", sources)

    def test_session_keys_are_opaque_confirmed_and_single_owner(self):
        pairing_header = (
            ROOT / "components/management_session/include/aiqa_pairing_crypto.h"
        ).read_text()
        channel_header = (
            ROOT / "components/management_session/include/aiqa_secure_channel.h"
        ).read_text()

        self.assertIn("typedef struct aiqa_pairing_keys aiqa_pairing_keys_t", pairing_header)
        self.assertNotIn("client_to_device_key[", pairing_header)
        normalized_channel_header = " ".join(channel_header.split())
        self.assertIn("both role-specific Finished proofs", normalized_channel_header)
        self.assertIn("one owning task", normalized_channel_header)
        self.assertIn("concurrent", normalized_channel_header)

    def test_pairing_code_is_not_public_persistent_or_logged(self):
        lifecycle_header = (
            ROOT / "components/management_session/include/aiqa_pairing_lifecycle.h"
        ).read_text()
        lifecycle_source = (
            ROOT / "components/management_session/src/aiqa_pairing_lifecycle.c"
        ).read_text()

        structs = {
            name: body
            for body, name in re.findall(
                r"typedef struct \{([^{}]*)\}\s+(\w+);",
                lifecycle_header,
                re.DOTALL,
            )
        }

        self.assertIn("aiqa_pairing_lock_record_t", structs)
        self.assertIn("aiqa_pairing_lifecycle_status_t", structs)
        self.assertNotIn("code", structs["aiqa_pairing_lock_record_t"].lower())
        self.assertNotIn("code", structs["aiqa_pairing_lifecycle_status_t"].lower())
        self.assertNotIn("ESP_LOG", lifecycle_source)
        self.assertNotIn("printf(", lifecycle_source)

    def test_pairing_lifecycle_has_no_runtime_or_transport_dependency(self):
        lifecycle_header = (
            ROOT / "components/management_session/include/aiqa_pairing_lifecycle.h"
        ).read_text()
        lifecycle_source = (
            ROOT / "components/management_session/src/aiqa_pairing_lifecycle.c"
        ).read_text()
        lifecycle = lifecycle_header + lifecycle_source

        self.assertNotIn("app_runtime", lifecycle)
        self.assertNotIn("management_service", lifecycle)
        self.assertNotIn("usb_serial_jtag", lifecycle)
        self.assertNotIn("nvs_", lifecycle)

    def test_pairing_lifecycle_declares_single_owner_and_callback_contracts(self):
        lifecycle_header = (
            ROOT / "components/management_session/include/aiqa_pairing_lifecycle.h"
        ).read_text()
        normalized = " ".join(lifecycle_header.split())

        self.assertIn("synchronously on one owner task", normalized)
        self.assertIn("callbacks must never re-enter", normalized)
        self.assertIn("neither retain nor log", normalized)
        self.assertIn("atomic durable commit/readback", normalized)
        self.assertIn("not enqueue deferred cleanup", normalized)
        self.assertIn("ACTIVE alone is insufficient", normalized)

    def test_esp_nvs_adapter_is_single_record_and_transport_independent(self):
        cmake = (
            ROOT / "components/management_session_esp/CMakeLists.txt"
        ).read_text()
        source = (
            ROOT
            / "components/management_session_esp/src/aiqa_pairing_esp_nvs.c"
        ).read_text()

        self.assertIn("REQUIRES", cmake)
        self.assertIn("management_session", cmake)
        self.assertIn("nvs_flash", cmake)
        for forbidden in ("app_runtime", "management_transport", "board_wave", "log"):
            self.assertNotIn(forbidden, cmake.lower())
        self.assertIn('AIQA_PAIRING_NVS_NAMESPACE "aiqa_pair"', source)
        self.assertIn('AIQA_PAIRING_NVS_LOCK_KEY "lock"', source)
        self.assertEqual(source.count("nvs_set_u64("), 1)
        self.assertNotIn("nvs_set_str(", source)
        self.assertNotIn("nvs_set_blob(", source)
        self.assertNotIn("ESP_LOG", source)
        self.assertLess(
            source.index("nvs_close(handle);", source.index("nvs_commit(handle)")),
            source.index("aiqa_pairing_esp_nvs_load_lock_record(context"),
        )

    def test_clear_code_failure_is_observable(self):
        lifecycle_header = (
            ROOT / "components/management_session/include/aiqa_pairing_lifecycle.h"
        ).read_text()
        normalized = " ".join(lifecycle_header.split())

        self.assertIn("bool (*clear_code)(void *context)", normalized)

    def test_management_usb_starts_before_slow_device_runtime(self):
        app_main = (ROOT / "main/app_main.c").read_text()

        self.assertLess(
            app_main.index("nvs_flash_init()"),
            app_main.index("aiqa_usb_management_start()"),
        )
        self.assertLess(
            app_main.index("aiqa_usb_management_start()"),
            app_main.index("aiqa_runtime_start()"),
        )
        self.assertNotIn("ESP_ERROR_CHECK(aiqa_runtime_start())", app_main)
        self.assertNotIn("nvs_flash_erase", app_main)


if __name__ == "__main__":
    unittest.main()
