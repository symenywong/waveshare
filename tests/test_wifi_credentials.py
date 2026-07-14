import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class WifiCredentialTests(unittest.TestCase):
    def test_wifi_validation_is_independent_from_api_keys_and_checks_boundaries(self):
        source = textwrap.dedent(
            """
            #include "aiqa_config.h"
            #include <assert.h>
            #include <stdio.h>
            #include <string.h>

            int main(void) {
                aiqa_secret_config_t secrets = {0};
                (void)snprintf(secrets.wifi_ssid, sizeof(secrets.wifi_ssid), "%s", "lab-wifi");
                (void)snprintf(secrets.wifi_password, sizeof(secrets.wifi_password), "%s", "test-password");
                assert(aiqa_wifi_secret_config_validate(&secrets) == AIQA_SECRET_OK);
                assert(aiqa_secret_config_validate(&secrets) == AIQA_SECRET_ERR_CHAT_API_KEY);

                aiqa_wifi_credentials_t credentials = {0};
                assert(aiqa_wifi_credentials_from_secrets(&secrets, &credentials));
                assert(strcmp(credentials.ssid, "lab-wifi") == 0);
                assert(strcmp(credentials.password, "test-password") == 0);
                assert(aiqa_wifi_credentials_validate(&credentials) == AIQA_SECRET_OK);
                assert(sizeof(credentials) == AIQA_MAX_WIFI_SSID_LEN + AIQA_MAX_WIFI_PASSWORD_LEN);

                secrets.wifi_ssid[0] = '\\0';
                assert(aiqa_wifi_secret_config_validate(&secrets) == AIQA_SECRET_ERR_WIFI_SSID);
                (void)memset(secrets.wifi_ssid, 's', 32);
                secrets.wifi_ssid[32] = '\\0';
                assert(aiqa_wifi_secret_config_validate(&secrets) == AIQA_SECRET_OK);

                secrets.wifi_password[0] = '\\0';
                assert(aiqa_wifi_secret_config_validate(&secrets) == AIQA_SECRET_OK);
                (void)snprintf(secrets.wifi_password, sizeof(secrets.wifi_password), "%s", "short");
                assert(aiqa_wifi_secret_config_validate(&secrets) == AIQA_SECRET_ERR_WIFI_PASSWORD);
                (void)memset(secrets.wifi_password, 'p', 63);
                secrets.wifi_password[63] = '\\0';
                assert(aiqa_wifi_secret_config_validate(&secrets) == AIQA_SECRET_OK);
                (void)memset(secrets.wifi_password, 'p', 64);
                secrets.wifi_password[64] = '\\0';
                assert(aiqa_wifi_secret_config_validate(&secrets) == AIQA_SECRET_ERR_WIFI_PASSWORD);
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as tmp:
            test_c = Path(tmp) / "wifi_credentials.c"
            binary = Path(tmp) / "wifi_credentials"
            test_c.write_text(source, encoding="utf-8")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{REPO_ROOT / 'components/config_store/include'}",
                    f"-I{REPO_ROOT / 'components/provider_common/include'}",
                    str(test_c),
                    str(REPO_ROOT / "components/config_store/src/aiqa_config.c"),
                    str(REPO_ROOT / "components/provider_common/src/aiqa_provider.c"),
                    "-o",
                    str(binary),
                ],
                cwd=REPO_ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
