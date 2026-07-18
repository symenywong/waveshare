import csv
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class ReleaseSecurityProfileTests(unittest.TestCase):
    def test_release_defaults_enable_at_rest_protection(self):
        defaults = (REPO_ROOT / "sdkconfig.release.defaults").read_text()
        required = {
            "CONFIG_SECURE_BOOT=y",
            "CONFIG_SECURE_BOOT_V2_ENABLED=y",
            "CONFIG_SECURE_BOOT_V2_RSA_ENABLED=y",
            "CONFIG_SECURE_SIGNED_APPS=y",
            "CONFIG_SECURE_SIGNED_ON_BOOT=y",
            "CONFIG_SECURE_SIGNED_ON_UPDATE=y",
            "CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y",
            "# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES is not set",
            "CONFIG_SECURE_FLASH_ENC_ENABLED=y",
            "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y",
            "# CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT is not set",
            "CONFIG_NVS_ENCRYPTION=y",
            "CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y",
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.release.csv"',
            "CONFIG_PARTITION_TABLE_OFFSET=0x10000",
        }
        self.assertTrue(required.issubset(set(defaults.splitlines())))

    def test_release_partition_protects_nvs_keys_only(self):
        with (REPO_ROOT / "partitions.release.csv").open(newline="") as source:
            rows = [
                [field.strip() for field in row]
                for row in csv.reader(
                    line for line in source if line.strip() and not line.startswith("#")
                )
            ]
        by_name = {row[0]: row for row in rows}
        self.assertEqual(by_name["nvs"][1:3], ["data", "nvs"])
        self.assertNotIn("encrypted", by_name["nvs"][5:])
        self.assertEqual(by_name["nvs_keys"][1:3], ["data", "nvs_keys"])
        self.assertEqual(by_name["nvs_keys"][4], "0x1000")
        self.assertIn("encrypted", by_name["nvs_keys"][5:])

    def test_release_builder_is_build_only_and_isolated(self):
        script = (REPO_ROOT / "scripts/build-release-security.sh").read_text()
        self.assertIn("build-release", script)
        self.assertIn("sdkconfig.release.defaults", script)
        self.assertIn(" build", script)
        for forbidden in (
            " encrypted-flash",
            " app-flash",
            " partition-table-flash",
            " erase-flash",
            " monitor",
            " espefuse",
        ):
            self.assertNotIn(forbidden, script)

    def test_remote_signer_requires_external_key_and_signs_both_images(self):
        script = (REPO_ROOT / "scripts/sign-release-security.sh").read_text()

        self.assertIn("AIQA_SECURE_BOOT_SIGNING_KEY", script)
        self.assertIn("bootloader/bootloader.bin", script)
        self.assertIn("waveshare_ai_pet.bin", script)
        self.assertEqual(script.count("secure-sign-data"), 2)
        self.assertEqual(script.count("signature_info_v2"), 2)
        self.assertNotIn("generate_signing_key", script)


if __name__ == "__main__":
    unittest.main()
