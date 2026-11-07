"""Run the actual status handler and Arduino sketch-size code with native IO."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path.home() / ".platformio/packages/framework-arduinoespressif32/cores/esp32/Esp.cpp"
JSON = ROOT / ".pio/libdeps/esp32-c6/ArduinoJson/src"


def braced(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += source[index] == "{"
        depth -= source[index] == "}"
        if depth == 0:
            return source[start:index + 1]
    raise ValueError("unbalanced source")


@unittest.skipUnless(SDK.exists() and JSON.exists(), "Install PlatformIO dependencies first")
class StatusOutputTest(unittest.TestCase):
    def test_polling_never_reverifies_firmware_and_keeps_live_fields(self):
        web = (ROOT / "firmware/src/web/WebConfigServer.cpp").read_text()
        types = (ROOT / "firmware/src/web/WebConfigTypes.h").read_text()
        sdk = SDK.read_text()
        handler = braced(web, "void WebConfigServer::handleStatus()")
        prime = braced(web, "  if (firmwareImageInfoReads_ == 0)")
        self.assertLess(web.index(prime), web.index("  WiFi.mode(WIFI_AP)"))
        legacy = handler.replace("firmwareImageInfo_.sketchSizeBytes", "ESP.getSketchSize()")
        legacy = legacy.replace("firmwareImageInfo_.freeSketchSpaceBytes", "ESP.getFreeSketchSpace()")
        with tempfile.TemporaryDirectory(prefix="ledbox-status-") as folder:
            temporary = Path(folder)
            (temporary / "sdk_sketch_size.inc").write_text(
                braced(sdk, "static uint32_t sketchSize(") + "\n" +
                braced(sdk, "uint32_t EspClass::getSketchSize()")
            )
            (temporary / "firmware_image_types.inc").write_text(
                braced(types, "struct FirmwareImageInfo") + ";\n" +
                braced(types, "struct WebConfigSettings") + ";\n"
            )
            (temporary / "firmware_image_read.inc").write_text(braced(web, "FirmwareImageInfo readFirmwareImageInfo()"))
            (temporary / "firmware_image_prime.inc").write_text(prime)
            for name, source, expected in [("previous", legacy, 1), ("cached", handler, 0)]:
                (temporary / "status_handler.inc").write_text(source)
                binary = temporary / name
                compilation = subprocess.run([
                    os.environ.get("CXX", "c++"), "-std=c++20", "-O2",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    f"-I{temporary}", f"-I{JSON}", f"-I{ROOT / 'test/native/stubs'}", f"-I{ROOT / 'firmware'}",
                    str(ROOT / "test/native/status_polling.cpp"),
                    str(ROOT / "firmware/src/diagnostics/Diagnostics.cpp"),
                    str(ROOT / "firmware/src/config/LightingConfig.cpp"), "-o", str(binary),
                ], capture_output=True, text=True)
                self.assertEqual(compilation.returncode, 0, compilation.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                print(f"{name}: {result.stdout.strip()}")
