"""Guard buffer ownership and complete frames in FastLED 3.10.3 RMT5.

The dependency lives in this project's generated .pio/libdeps directory.
Wait for the previous transfer BEFORE writing its pixel buffer, not only
when drawAsync() starts the next transfer. No timing delays or FPS guesses.
Complete showPixels() before returning, including the final black frame.
Forward the requested interrupt priority and keep the streaming encoder in IRAM.
"""

import json
from pathlib import Path

ANCHOR = "    if (is_rgbw) {\n"
GUARD = (
    "    // LedBox: the previous RMT transfer owns this buffer until completion.\n"
    "    mLedStrip->waitDone();\n"
)
ASYNC_SHOW = "    mLedStrip->drawAsync();\n"
SYNC_SHOW = (
    "    // LedBox: finish this frame even when no next show() follows.\n"
    "    mLedStrip->drawSync();\n"
)
PRIORITY_ANCHOR = "        .trans_queue_depth = LED_STRIP_RMT_DEFAULT_TRANS_QUEUE_SIZE,\n"
PRIORITY_FIELD = "        .intr_priority = rmt_config->interrupt_priority,\n"
QUANTIZATION_PREP = '''    // LedBox: preserve the requested complete bit period when quantizing ticks.
    const uint32_t zero_ticks = (((uint64_t)config->timings.t0h + config->timings.t0l) * config->resolution + 999999999ULL) / 1000000000ULL;
    const uint32_t one_ticks = (((uint64_t)config->timings.t1h + config->timings.t1l) * config->resolution + 999999999ULL) / 1000000000ULL;
    const uint32_t zero_high_ticks = (uint64_t)config->timings.t0h * config->resolution / 1000000000ULL;
    const uint32_t one_high_ticks = (uint64_t)config->timings.t1h * config->resolution / 1000000000ULL;
'''
QUANTIZATION_FIELDS = {
    "(float)config->timings.t0h * config->resolution / 1000000000": "zero_high_ticks",
    "(float)config->timings.t0l * config->resolution / 1000000000": "zero_ticks - zero_high_ticks",
    "(float)config->timings.t1h * config->resolution / 1000000000": "one_high_ticks",
    "(float)config->timings.t1l * config->resolution / 1000000000": "one_ticks - one_high_ticks",
}
if "Import" in globals():
    Import("env")
    PROJECT_ROOT = Path(env.subst("$PROJECT_DIR"))
else:
    PROJECT_ROOT = Path(__file__).resolve().parents[1]
HOOK_INCLUDE = '#include "' + str(
    PROJECT_ROOT / "firmware/src/diagnostics/RmtDiagnosticsHooks.h"
) + '"\n'


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError("FastLED RMT5 source changed; review diagnostic hooks")
    return source.replace(old, new, 1)


def quantized_encoder_source(source):
    if QUANTIZATION_PREP in source:
        return source
    anchor = "    rmt_bytes_encoder_config_t bytes_encoder_config = {\n"
    result = replace_once(source, anchor, QUANTIZATION_PREP + anchor)
    for original, corrected in QUANTIZATION_FIELDS.items():
        result = replace_once(result, original, corrected)
    return result


def telemetry_device_source(source):
    if "// LedBox: RMT task-context telemetry" in source:
        return source
    source = replace_once(source, '#include "led_strip_rmt_encoder.h"\n',
                          '#include "led_strip_rmt_encoder.h"\n' + HOOK_INCLUDE)
    start = source.index("static esp_err_t led_strip_rmt_refresh_async(")
    end = source.index("static esp_err_t led_strip_rmt_refresh(", start)
    source = source[:start] + '''// LedBox: RMT task-context telemetry; no encoder ISR instrumentation.
static void ledbox_report_result(uint8_t operation, esp_err_t code)
{
    if (ledbox_rmt_result) {
        const LedboxRmtResult result = { .operation = operation, .code = code };
        ledbox_rmt_result(&result);
    }
}

static void ledbox_report_payload(led_strip_rmt_obj *strip, uint8_t completed)
{
    if (ledbox_rmt_payload) {
        const LedboxRmtPayload payload = {
            .bytes = strip->pixel_buf,
            .size = strip->strip_len * strip->bytes_per_pixel,
            .completed = completed,
        };
        ledbox_rmt_payload(&payload);
    }
}

static esp_err_t led_strip_rmt_refresh_async(led_strip_t *strip)
{
    led_strip_rmt_obj *rmt_strip = __containerof(strip, led_strip_rmt_obj, base);
    const esp_err_t enabled = rmt_enable(rmt_strip->rmt_chan);
    ledbox_report_result(1, enabled);
    ESP_RETURN_ON_ERROR(enabled, TAG, "enable RMT channel failed");
    ledbox_report_payload(rmt_strip, 0);
    const esp_err_t submitted = rmt_transmit(rmt_strip->rmt_chan, rmt_strip->strip_encoder,
        rmt_strip->pixel_buf, rmt_strip->strip_len * rmt_strip->bytes_per_pixel, &rmt_strip->tx_conf);
    ledbox_report_result(2, submitted);
    ESP_RETURN_ON_ERROR(submitted, TAG, "transmit pixels by RMT failed");
    return ESP_OK;
}

static esp_err_t led_strip_rmt_wait_for_done(led_strip_t *strip)
{
    led_strip_rmt_obj *rmt_strip = __containerof(strip, led_strip_rmt_obj, base);
    const esp_err_t completed = rmt_tx_wait_all_done(rmt_strip->rmt_chan, -1);
    ledbox_report_result(3, completed);
    ESP_RETURN_ON_ERROR(completed, TAG, "wait for RMT done failed");
    ledbox_report_payload(rmt_strip, 1);
    const esp_err_t disabled = rmt_disable(rmt_strip->rmt_chan);
    ledbox_report_result(4, disabled);
    ESP_RETURN_ON_ERROR(disabled, TAG, "disable RMT channel failed");
    return ESP_OK;
}

''' + source[end:]
    anchor = '    ESP_GOTO_ON_ERROR(rmt_new_tx_channel(&rmt_chan_config, &rmt_strip->rmt_chan), err, TAG, "create RMT TX channel failed");\n'
    return replace_once(source, anchor, anchor + '''    if (ledbox_rmt_channel) {
        const LedboxRmtChannel measured = {
            .pin = rmt_chan_config.gpio_num,
            .resolution_hz = rmt_chan_config.resolution_hz,
            .memory_symbols = rmt_chan_config.mem_block_symbols,
            .queue_depth = rmt_chan_config.trans_queue_depth,
            .configured_priority = rmt_chan_config.intr_priority,
            .dma = rmt_chan_config.flags.with_dma,
            .inverted = rmt_chan_config.flags.invert_out,
        };
        ledbox_rmt_channel(&measured);
    }
''')


def telemetry_encoder_source(source):
    if "// LedBox: actual encoder tick configuration" in source:
        return source
    source = replace_once(source, '#include "esp_attr.h"\n', '#include "esp_attr.h"\n' + HOOK_INCLUDE)
    anchor = '    *ret_encoder = &led_encoder->base;\n'
    return replace_once(source, anchor, '''    // LedBox: actual encoder tick configuration, recorded outside the ISR.
    if (ledbox_rmt_timings) {
        const LedboxRmtTimings measured = {
            .resolution_hz = config->resolution,
            .zero_high_ticks = bytes_encoder_config.bit0.duration0,
            .zero_low_ticks = bytes_encoder_config.bit0.duration1,
            .one_high_ticks = bytes_encoder_config.bit1.duration0,
            .one_low_ticks = bytes_encoder_config.bit1.duration1,
            .reset_ticks = led_encoder->reset_code.duration0 + led_encoder->reset_code.duration1,
        };
        ledbox_rmt_timings(&measured);
    }
''' + anchor)


def guarded_source(source):
    if source.count(ANCHOR) != 1:
        raise RuntimeError("FastLED RMT5 source changed; review the buffer guard")
    guarded = source if GUARD in source else source.replace(ANCHOR, GUARD + ANCHOR, 1)
    if SYNC_SHOW in guarded:
        return guarded
    if guarded.count(ASYNC_SHOW) != 1:
        raise RuntimeError("FastLED RMT5 show function changed; review frame completion")
    return guarded.replace(ASYNC_SHOW, SYNC_SHOW, 1)


def priority_source(source):
    if source.count(PRIORITY_ANCHOR) != 1:
        raise RuntimeError("FastLED RMT5 channel configuration changed; review interrupt priority")
    if PRIORITY_FIELD in source:
        return source
    return source.replace(PRIORITY_ANCHOR, PRIORITY_ANCHOR + PRIORITY_FIELD, 1)


def encoder_source(source):
    include = '#include "esp_check.h"\n'
    if source.count(include) != 1:
        raise RuntimeError("FastLED RMT5 encoder changed; review IRAM placement")
    included = source if '#include "esp_attr.h"' in source else source.replace(
        include, include + '#include "esp_attr.h"\n', 1
    )
    encode = "static size_t rmt_encode_led_strip("
    reset = "static esp_err_t rmt_led_strip_encoder_reset("
    iram_encode = "static size_t IRAM_ATTR rmt_encode_led_strip("
    iram_reset = "static esp_err_t IRAM_ATTR rmt_led_strip_encoder_reset("
    if (encode not in included and iram_encode not in included) or (
        reset not in included and iram_reset not in included
    ):
        raise RuntimeError("FastLED RMT5 encoder functions changed; review IRAM placement")
    return included.replace(encode, iram_encode, 1).replace(reset, iram_reset, 1)


def guard_rmt_buffer(env, node):
    source = Path(node.srcnode().get_abspath())
    manifest = json.loads((source.parents[5] / "library.json").read_text())
    if manifest["version"] != "3.10.3":
        raise RuntimeError("Review the RMT5 backport before changing FastLED version")
    original = source.read_text()
    transform = {
        "idf5_rmt.cpp": guarded_source,
        "led_strip_rmt_dev.c": lambda value: telemetry_device_source(priority_source(value)),
        "led_strip_rmt_encoder.c": lambda value: quantized_encoder_source(telemetry_encoder_source(encoder_source(value))),
    }[source.name]
    guarded = transform(original)
    if guarded != original:
        source.write_text(guarded)
        print(f"FastLED RMT5: applied LedBox backport to {source.name}")
    return node


if "Import" in globals():
    env.AddBuildMiddleware(
        guard_rmt_buffer, "*/FastLED/src/platforms/esp/32/rmt_5/idf5_rmt.cpp"
    )
    env.AddBuildMiddleware(
        guard_rmt_buffer,
        "*/FastLED/src/third_party/espressif/led_strip/src/led_strip_rmt_dev.c",
    )
    env.AddBuildMiddleware(
        guard_rmt_buffer,
        "*/FastLED/src/third_party/espressif/led_strip/src/led_strip_rmt_encoder.c",
    )
