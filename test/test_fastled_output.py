"""Exercise the installed, pinned dependency, not a mock of its algorithms."""

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FASTLED = ROOT / ".pio/libdeps/esp32-c6/FastLED/src"
RMT = FASTLED / "platforms/esp/32/rmt_5"
SPEC = importlib.util.spec_from_file_location(
    "buffer_guard", ROOT / "scripts/fastled_rmt5_buffer_guard.py"
)
GUARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUARD)


@unittest.skipUnless(FASTLED.exists(), "Install PlatformIO dependencies to run real FastLED tests")
class FastLedOutputTest(unittest.TestCase):
    def test_real_encoder_does_not_shorten_ws2811_bit_period(self):
        encoder = FASTLED / "third_party/espressif/led_strip/src/led_strip_rmt_encoder.c"
        installed = encoder.read_text()
        original = installed.replace(GUARD.QUANTIZATION_PREP, "")
        for before, after in GUARD.QUANTIZATION_FIELDS.items():
            # Undo only duration assignments; the telemetry hook has fields
            # with similar names which must remain untouched.
            original = original.replace("= " + after + ",", "= " + before + ",")
        corrected = GUARD.quantized_encoder_source(original)
        self.assertEqual(GUARD.quantized_encoder_source(corrected), corrected)
        prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { unsigned level0, duration0, level1, duration1; } symbol;
typedef struct { symbol bit0, bit1; struct { unsigned msb_first; } flags; } rmt_bytes_encoder_config_t;
int main(void) {
    const struct {
        struct { unsigned t0h, t0l, t1h, t1l; } timings;
        unsigned resolution;
    } requested = {{320, 960, 640, 640}, 10000000}, *config = &requested;
'''
        with tempfile.TemporaryDirectory(prefix="ledbox-encoder-period-") as folder:
            for name, source, ticks in [("previous", original, 12), ("corrected", corrected, 13)]:
                anchor = "    rmt_bytes_encoder_config_t bytes_encoder_config = {"
                start = source.index(GUARD.QUANTIZATION_PREP) if name == "corrected" else source.index(anchor)
                end = source.index("    };", source.index(anchor)) + len("    };")
                suffix = f'''
    assert(bytes_encoder_config.bit0.duration0 == 3);
    assert(bytes_encoder_config.bit1.duration0 == 6);
    assert(bytes_encoder_config.bit0.duration0 + bytes_encoder_config.bit0.duration1 == {ticks});
    assert(bytes_encoder_config.bit1.duration0 + bytes_encoder_config.bit1.duration1 == {ticks});
    printf("0=%u+%u ticks; 1=%u+%u ticks\\n", bytes_encoder_config.bit0.duration0,
      bytes_encoder_config.bit0.duration1, bytes_encoder_config.bit1.duration0, bytes_encoder_config.bit1.duration1);
}}
'''
                path = Path(folder) / (name + ".c")
                binary = Path(folder) / name
                path.write_text(prefix + source[start:end] + suffix)
                compilation = subprocess.run([
                    os.environ.get("CC", "cc"), "-std=c11", str(path), "-o", str(binary),
                ], capture_output=True, text=True)
                self.assertEqual(compilation.returncode, 0, compilation.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
                print(f"{name}: {result.stdout.strip()}")

    def test_real_driver_telemetry_callbacks_and_error_paths(self):
        driver = FASTLED / "third_party/espressif/led_strip/src/led_strip_rmt_dev.c"
        patched = GUARD.telemetry_device_source(driver.read_text())
        self.assertEqual(GUARD.telemetry_device_source(patched), patched)
        start = patched.index("// LedBox: RMT task-context telemetry")
        end = patched.index("static esp_err_t led_strip_rmt_refresh(", start)
        prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define __containerof(ptr, type, member) ((type *)(ptr))
#define ESP_RETURN_ON_ERROR(code, tag, msg) do { int rc = (code); if (rc) return rc; } while (0)
typedef struct { int unused; } led_strip_t;
typedef struct {
    led_strip_t base;
    void *rmt_chan;
    void *strip_encoder;
    int tx_conf;
    unsigned strip_len;
    unsigned bytes_per_pixel;
    unsigned char *pixel_buf;
} led_strip_rmt_obj;
static int fail_operation, mutate, submissions, completions, result_count, errors;
static unsigned char before[6];
static unsigned char *pending;
static int rmt_enable(void *channel) { (void)channel; return fail_operation == 1 ? 101 : 0; }
static int rmt_transmit(void *channel, void *encoder, const void *bytes, size_t size, const void *config) {
    (void)channel; (void)encoder; (void)config;
    assert(size == 6);
    pending = (unsigned char *)bytes;
    return fail_operation == 2 ? 102 : 0;
}
static int rmt_tx_wait_all_done(void *channel, int timeout) {
    (void)channel; assert(timeout == -1);
    if (mutate) pending[5] = 255;
    return fail_operation == 3 ? 103 : 0;
}
static int rmt_disable(void *channel) { (void)channel; return fail_operation == 4 ? 104 : 0; }
void ledbox_rmt_payload(const LedboxRmtPayload *payload) {
    assert(payload->size == 6);
    if (!payload->completed) { ++submissions; memcpy(before, payload->bytes, 6); return; }
    ++completions;
    assert((memcmp(before, payload->bytes, 6) != 0) == (mutate != 0));
}
void ledbox_rmt_result(const LedboxRmtResult *result) {
    ++result_count;
    if (result->code) { ++errors; assert(result->code == 100 + result->operation); }
}
'''
        suffix = r'''
int main(void) {
    for (int failed = 0; failed <= 4; ++failed) {
        unsigned char bytes[] = {80,80,80,80,80,40};
        led_strip_rmt_obj strip = {.strip_len=2, .bytes_per_pixel=3, .pixel_buf=bytes};
        fail_operation = failed;
        submissions = completions = result_count = errors = 0;
        int result = led_strip_rmt_refresh_async(&strip.base);
        if (!result) result = led_strip_rmt_wait_for_done(&strip.base);
        assert(result == (failed ? 100 + failed : 0));
        assert(errors == (failed != 0));
        assert(submissions == (failed != 1));
        assert(completions == (failed == 0 || failed == 4));
        assert(result_count == (failed ? failed : 4));
    }
    unsigned char bytes[] = {1,2,3,4,5,6};
    led_strip_rmt_obj strip = {.strip_len=2, .bytes_per_pixel=3, .pixel_buf=bytes};
    mutate = 1; fail_operation = 0;
    assert(led_strip_rmt_refresh_async(&strip.base) == 0);
    assert(led_strip_rmt_wait_for_done(&strip.base) == 0);
    puts("Real RMT C driver: actual payload hooks, post-wait mutation and all four API failure paths observed");
}
'''
        with tempfile.TemporaryDirectory(prefix="ledbox-rmt-telemetry-") as folder:
            path = Path(folder) / "telemetry.c"
            binary = Path(folder) / "telemetry"
            path.write_text(GUARD.HOOK_INCLUDE + prefix + patched[start:end] + suffix)
            compilation = subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer", str(path), "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(compilation.returncode, 0, compilation.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
            print(result.stdout.strip())

    def test_requested_interrupt_priority_reaches_idf_channel_config(self):
        driver = FASTLED / "third_party/espressif/led_strip/src/led_strip_rmt_dev.c"
        installed = driver.read_text()
        original = installed.replace(GUARD.PRIORITY_FIELD, "")
        patched = GUARD.priority_source(original)
        self.assertEqual(GUARD.priority_source(patched), patched)
        # Compile the actual initializer passed to rmt_new_tx_channel(), with
        # minimal API-shaped inputs. The requested priority must not be lost.
        prefix = """
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#define LED_STRIP_RMT_DEFAULT_TRANS_QUEUE_SIZE 4
typedef struct {
    int clk_src;
    int gpio_num;
    size_t mem_block_symbols;
    uint32_t resolution_hz;
    unsigned trans_queue_depth;
    int intr_priority;
    struct { unsigned with_dma; unsigned invert_out; } flags;
} rmt_tx_channel_config_t;
int main(void) {
    const struct { int interrupt_priority; struct { unsigned with_dma; } flags; }
        requested = { .interrupt_priority = 3, .flags = { .with_dma = 0 } },
        *rmt_config = &requested;
    const struct { int strip_gpio_num; struct { unsigned invert_out; } flags; }
        led = { .strip_gpio_num = 4, .flags = { .invert_out = 0 } }, *led_config = &led;
    int clk_src = 0;
    size_t mem_block_symbols = 48;
    uint32_t resolution = 10000000;
"""
        with tempfile.TemporaryDirectory(prefix="ledbox-priority-") as folder:
            for name, source, expected in [("original", original, "0"), ("patched", patched, "3")]:
                start = source.index("    rmt_tx_channel_config_t rmt_chan_config = {")
                end = source.index("    };", start) + len("    };")
                program = prefix + source[start:end] + '\n printf("%d", rmt_chan_config.intr_priority);\n}\n'
                path = Path(folder) / f"{name}.c"
                binary = Path(folder) / name
                path.write_text(program)
                compilation = subprocess.run([
                    os.environ.get("CC", "cc"), "-std=c11", str(path), "-o", str(binary),
                ], capture_output=True, text=True)
                self.assertEqual(compilation.returncode, 0, compilation.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
                self.assertEqual(result.stdout, expected)
                print(f"{name}: requested RMT priority 3 -> IDF priority {result.stdout}")

    def test_real_pixel_scaling_and_dithering(self):
        with tempfile.TemporaryDirectory(prefix="ledbox-pixels-") as folder:
            binary = Path(folder) / "pixels"
            compilation = subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++17", "-O2",
                "-DFASTLED_STUB_IMPL", f"-I{FASTLED}",
                str(ROOT / "test/native/fastled_pixels.cpp"), "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(compilation.returncode, 0, compilation.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
            print(result.stdout.strip())

    def test_real_rmt_ownership_before_and_after_backport(self):
        installed = (RMT / "idf5_rmt.cpp").read_text()
        original = installed.replace(GUARD.GUARD, "").replace(GUARD.SYNC_SHOW, GUARD.ASYNC_SHOW)
        buffer_guard_only = original.replace(GUARD.ANCHOR, GUARD.GUARD + GUARD.ANCHOR, 1)
        guarded = GUARD.guarded_source(original)
        self.assertEqual(GUARD.guarded_source(guarded), guarded)
        with tempfile.TemporaryDirectory(prefix="ledbox-rmt-") as folder:
            for name, source, expected in [
                ("original", original, 1),
                ("buffer guard only", buffer_guard_only, 2),
                ("guarded and synchronous", guarded, 0),
            ]:
                cpp = Path(folder) / f"{name}.cpp"
                binary = Path(folder) / name
                cpp.write_text(source)
                compilation = subprocess.run([
                    os.environ.get("CXX", "c++"), "-std=c++17", "-O2",
                    "-DESP32", "-DFASTLED_RMT5=1",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    f"-I{ROOT / 'test/native/rmt_stubs'}", f"-I{RMT}", f"-I{FASTLED}",
                    str(cpp), str(ROOT / "test/native/fastled_rmt.cpp"), "-o", str(binary),
                ], capture_output=True, text=True)
                self.assertEqual(compilation.returncode, 0, compilation.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                print(f"{name}: {(result.stdout + result.stderr).strip()}")


if __name__ == "__main__":
    unittest.main()
