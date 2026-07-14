import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ManagementWireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "management_wire_harness"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                f"-I{ROOT / 'components/management_transport/include'}",
                str(ROOT / "tests/c/management_wire_harness.c"),
                str(ROOT / "components/management_transport/src/aiqa_management_wire.c"),
                "-o",
                str(cls.binary),
            ],
            cwd=ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def run_case(self, case: str) -> None:
        subprocess.run([str(self.binary), case], cwd=ROOT, check=True)

    def test_encodes_versioned_big_endian_header(self):
        self.run_case("encode")

    def test_decodes_a_frame_split_at_every_byte(self):
        self.run_case("partial")

    def test_decodes_multiple_frames_from_one_usb_read(self):
        self.run_case("coalesced")

    def test_resynchronizes_after_non_protocol_bytes(self):
        self.run_case("resync")

    def test_rejects_oversized_payload_before_allocation(self):
        self.run_case("oversize")

    def test_delivers_an_empty_event_frame(self):
        self.run_case("empty")

    def test_rejects_invalid_arguments_versions_kinds_and_flags(self):
        self.run_case("invalid")


if __name__ == "__main__":
    unittest.main()
