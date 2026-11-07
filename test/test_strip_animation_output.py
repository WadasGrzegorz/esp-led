"""Exercise the actual WebConfigServer test handlers with native LED/NVS IO."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = ROOT / ".pio/libdeps/esp32-c6/ArduinoJson/src"


def braced(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise ValueError("unbalanced source")


@unittest.skipUnless(JSON.exists(), "Install PlatformIO dependencies first")
class StripAnimationOutputTest(unittest.TestCase):
    def test_actual_handlers_select_validate_finish_stop_and_preserve_nvs(self):
        web = (ROOT / "firmware/src/web/WebConfigServer.cpp").read_text()
        signatures = [
            "void WebConfigServer::handleAnimationTest()",
            "void WebConfigServer::handleStripTest()",
            "void WebConfigServer::handleDiagnosticRun()",
            "void WebConfigServer::handleStopTest()",
            "void WebConfigServer::updateAnimationTest(",
            "void WebConfigServer::stopVisualActions(",
        ]
        with tempfile.TemporaryDirectory(prefix="ledbox-strip-api-") as folder:
            temporary = Path(folder)
            (temporary / "web_strip_handlers.inc").write_text(
                "\n\n".join(braced(web, signature) for signature in signatures)
            )
            binary = temporary / "strip-api"
            compilation = subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++17", "-O2",
                "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer", f"-I{temporary}", f"-I{JSON}",
                f"-I{ROOT / 'test/native/stubs'}", f"-I{ROOT / 'firmware'}",
                str(ROOT / "test/native/strip_animation_api.cpp"),
                *[str(ROOT / "firmware/src" / path) for path in [
                    "config/LightingConfig.cpp", "config/ConfigurationManager.cpp",
                    "setup/SetupController.cpp", "led/LedController.cpp",
                    "led/AnimationEngine.cpp", "diagnostics/Diagnostics.cpp",
                    "diagnostics/DiagnosticsJson.cpp", "diagnostics/OutputDiagnostic.cpp",
                ]], "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(compilation.returncode, 0, compilation.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())
