#pragma once

#include <Arduino.h>

#include "src/config/LightingConfig.h"

namespace hardware {
constexpr uint8_t DATA_PIN = 4;
constexpr uint8_t LEFT_PIR_PIN = 5;
constexpr uint8_t RIGHT_PIR_PIN = 6;
constexpr uint8_t BH1750_SDA_PIN = 2;
constexpr uint8_t BH1750_SCL_PIN = 3;
// ESP32-C6-DevKitM-1 BOOT button: GPIO9, active LOW. GPIO9 is a strapping
// pin, so CONFIG MODE is intentionally entered by holding BOOT after the
// application has started, not while resetting/powering the board.
constexpr uint8_t CONFIG_BUTTON_PIN = 9;
constexpr uint8_t BH1750_LOW_ADDRESS = 0x23;
constexpr uint8_t BH1750_HIGH_ADDRESS = 0x5C;
constexpr uint16_t WS2811_CONTROLLER_COUNT = 60;
constexpr uint16_t LOGICAL_PIXEL_COUNT = WS2811_CONTROLLER_COUNT * 3;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t PIR_STABILIZATION_MS = 10000;
constexpr uint32_t PIR_DEBOUNCE_MS = 80;
constexpr uint32_t PIR_MIN_RETRIGGER_MS = 2000;
constexpr uint32_t LUX_LOG_INTERVAL_MS = 1000;
constexpr uint32_t BH1750_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t LUX_AFTER_LED_SETTLE_MS = 300;
constexpr uint32_t CONFIG_BUTTON_HOLD_MS = 3000;
constexpr uint32_t CONFIG_MODE_TIMEOUT_MS = 5UL * 60UL * 1000UL;
constexpr uint8_t CONFIG_TEST_MAX_BRIGHTNESS = 96;
constexpr uint32_t CONFIG_STRIP_TEST_TIMEOUT_MS = 30UL * 1000UL;
constexpr uint32_t CONFIG_SAVE_RESTART_DELAY_MS = 900;
}  // namespace hardware

// Safe factory geometry for the currently verified chain. Runtime code loads
// the active geometry from NVS and falls back to this value if none is valid.
constexpr LightingConfig DEFAULT_LIGHTING_CONFIG = {
    LIGHTING_CONFIG_VERSION,
    4,      // stripCount
    80,     // maxBrightness (0-255)
    1680,   // stripFadeInMs
    0.80F,  // nextStripStartProgress
    StripStartMode::CASCADE,
    3000,   // holdMs
    2640,   // fadeOutMs
    FadeOutStyle::GLOBAL,
    2.2F,   // gamma
    true,   // enableWaterfall
    WaterfallDirection::TOP_TO_BOTTOM,
    40,     // waterfallSpeedPps
    false,  // enableLuxGate; temporarily disabled while tuning animation
    15.0F,  // luxThreshold; used only when enableLuxGate is true
    {
        {18, false},
        {54, false},
        {54, false},
        {54, false},
    },
};

static_assert(18 + 54 + 54 + 54 == hardware::LOGICAL_PIXEL_COUNT,
              "Factory geometry must cover the verified 180-section chain");
