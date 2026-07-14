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


if __name__ == "__main__":
    unittest.main()
