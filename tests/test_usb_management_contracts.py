import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbManagementContractTests(unittest.TestCase):
    def test_usb_management_port_is_not_a_secondary_log_console(self):
        defaults = (ROOT / "sdkconfig.defaults").read_text()
        generated = (ROOT / "sdkconfig").read_text()

        self.assertIn("CONFIG_ESP_CONSOLE_UART_DEFAULT=y", defaults)
        self.assertIn("CONFIG_ESP_CONSOLE_SECONDARY_NONE=y", defaults)
        self.assertNotIn("CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y", generated)
        self.assertIn("CONFIG_ESP_CONSOLE_SECONDARY_NONE=y", generated)

    def test_management_uses_native_usb_serial_jtag(self):
        defaults = (ROOT / "sdkconfig.defaults").read_text()
        manifest_path = ROOT / "components/management_transport/idf_component.yml"
        manifest = manifest_path.read_text() if manifest_path.exists() else ""
        cmake = (
            ROOT / "components/management_transport/CMakeLists.txt"
        ).read_text()

        self.assertNotIn("CONFIG_TINYUSB_CDC_ENABLED=y", defaults)
        self.assertNotIn("CONFIG_TINYUSB_CDC_COUNT=1", defaults)
        self.assertNotIn("esp_tinyusb", manifest)
        self.assertIn("esp_driver_usb_serial_jtag", cmake)
        self.assertNotIn("aiqa_tinyusb_cdc_link.c", cmake)

    def test_owner_uses_usb_serial_jtag_data_path(self):
        source = (
            ROOT / "components/management_transport/src/aiqa_usb_management.c"
        ).read_text()
        self.assertIn("usb_serial_jtag_driver_install", source)
        self.assertIn("usb_serial_jtag_read_bytes", source)
        self.assertIn("usb_serial_jtag_write_bytes", source)
        self.assertIn("usb_serial_jtag_wait_tx_done", source)
        self.assertNotIn("aiqa_tinyusb_cdc_link", source)
        self.assertNotIn("tinyusb_", source)

    def test_transport_owns_pairing_and_authenticated_protocol_gates(self):
        source = (
            ROOT
            / "components/management_transport/src/aiqa_usb_management.c"
        ).read_text()

        self.assertIn("aiqa_management_protocol_handle_public_request", source)
        self.assertIn("aiqa_management_protocol_handle_authenticated_request", source)
        self.assertIn("aiqa_pairing_rpc_handle", source)
        self.assertIn("aiqa_secure_channel_decrypt", source)
        self.assertIn("aiqa_management_access_global_revoke", source)
        self.assertIn("AIQA_MANAGEMENT_WIRE_REQUEST", source)
        self.assertIn("aiqa_runtime_management_get_status", source)
        self.assertIn("aiqa_runtime_management_submit_wifi_update", source)
        self.assertNotIn("password", source.lower())

    def test_transport_does_not_log_untrusted_payloads(self):
        source = (
            ROOT
            / "components/management_transport/src/aiqa_usb_management.c"
        ).read_text()

        for log_line in (
            line for line in source.splitlines() if "ESP_LOG" in line
        ):
            self.assertNotIn("payload", log_line.lower())
            self.assertNotIn("response", log_line.lower())

    def test_native_usb_management_does_not_gate_rx_on_line_state(self):
        source = (
            ROOT
            / "components/management_transport/src/aiqa_usb_management.c"
        ).read_text()
        self.assertNotIn("usb_serial_jtag_is_connected", source)
        self.assertNotIn("update_connection(owner)", source)
        self.assertIn("owner->connected = true", source)

    def test_owner_binds_authorization_and_rx_to_link_generation(self):
        source = (
            ROOT
            / "components/management_transport/src/aiqa_usb_management.c"
        ).read_text()

        self.assertIn("link_generation", source)
        read = source[source.index("const int read") :]
        self.assertIn("read_generation", read)
        self.assertIn("usb_serial_jtag_read_bytes", read)
        finished = source[source.index("if (requires_tx_confirmation)") :]
        self.assertIn("link_epoch_is_current(owner)", finished)


if __name__ == "__main__":
    unittest.main()
