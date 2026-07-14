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

    def test_transport_only_exposes_the_public_protocol_gate(self):
        source = (
            ROOT
            / "components/management_transport/src/aiqa_usb_management.c"
        ).read_text()

        self.assertIn("aiqa_management_protocol_handle_public_request", source)
        self.assertIn("AIQA_MANAGEMENT_WIRE_REQUEST", source)
        self.assertNotIn("aiqa_runtime_management_", source)
        self.assertNotIn("management_service", source)
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


if __name__ == "__main__":
    unittest.main()
