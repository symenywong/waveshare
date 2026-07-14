import os
import subprocess
import unittest
from pathlib import Path


def build_vendored_mbedcrypto(build_dir: Path) -> tuple[Path, Path]:
    idf_path = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf"))
    source = idf_path / "components/mbedtls/mbedtls"
    if not (source / "library/ecjpake.c").exists():
        raise unittest.SkipTest("ESP-IDF vendored MbedTLS unavailable")

    subprocess.run(
        [
            "cmake",
            "-S",
            str(source),
            "-B",
            str(build_dir),
            "-DENABLE_PROGRAMS=OFF",
            "-DENABLE_TESTING=OFF",
            "-DUSE_SHARED_MBEDTLS_LIBRARY=OFF",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "mbedcrypto", "-j", "4"],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    return source / "include", build_dir / "library/libmbedcrypto.a"
