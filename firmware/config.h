#pragma once

#include <Arduino.h>

struct StripConfig {
  uint16_t startPixel;
  uint16_t pixelCount;
  bool reversed;
};

namespace hardware {
constexpr uint8_t DATA_PIN = 4;
constexpr uint8_t LEFT_PIR_PIN = 5;
constexpr uint8_t RIGHT_PIR_PIN = 6;
constexpr uint8_t BH1750_SDA_PIN = 2;
constexpr uint8_t BH1750_SCL_PIN = 3;
constexpr uint8_t BH1750_LOW_ADDRESS = 0x23;
constexpr uint8_t BH1750_HIGH_ADDRESS = 0x5C;
constexpr uint16_t WS2811_CONTROLLER_COUNT = 60;
constexpr uint16_t LOGICAL_PIXEL_COUNT = WS2811_CONTROLLER_COUNT * 3;
constexpr uint8_t STRIP_COUNT = 4;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t PIR_STABILIZATION_MS = 10000;
constexpr uint32_t LUX_LOG_INTERVAL_MS = 1000;
constexpr uint32_t BH1750_RETRY_INTERVAL_MS = 5000;
}  // namespace hardware

struct LightingConfig {
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float gamma;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[hardware::STRIP_COUNT];
};

// The verified 5 m roll has 180 logical monochrome sections, or 36 per metre.
// From the controller/DIN end, strip 1 is the 0.5 m cut (18 sections), followed
// by the three 1.5 m cuts (54 sections each). The starts remain contiguous.
constexpr uint16_t STRIP_1_PIXEL_COUNT = 18;
constexpr uint16_t STRIP_2_PIXEL_COUNT = 54;
constexpr uint16_t STRIP_3_PIXEL_COUNT = 54;
constexpr uint16_t STRIP_4_PIXEL_COUNT = 54;

constexpr uint16_t STRIP_1_START_PIXEL = 0;
constexpr uint16_t STRIP_2_START_PIXEL =
    STRIP_1_START_PIXEL + STRIP_1_PIXEL_COUNT;
constexpr uint16_t STRIP_3_START_PIXEL =
    STRIP_2_START_PIXEL + STRIP_2_PIXEL_COUNT;
constexpr uint16_t STRIP_4_START_PIXEL =
    STRIP_3_START_PIXEL + STRIP_3_PIXEL_COUNT;

static_assert(STRIP_1_PIXEL_COUNT > 0 && STRIP_2_PIXEL_COUNT > 0 &&
                  STRIP_3_PIXEL_COUNT > 0 && STRIP_4_PIXEL_COUNT > 0,
              "Every strip must contain at least one logical pixel");
static_assert(STRIP_4_START_PIXEL + STRIP_4_PIXEL_COUNT ==
                  hardware::LOGICAL_PIXEL_COUNT,
              "Configured strips must cover all 180 logical sections");

constexpr LightingConfig lightingConfig = {
    80,     // maxBrightness (0-255)
    1680,   // stripFadeInMs
    0.80F,  // nextStripStartProgress
    3000,   // holdMs
    2640,   // fadeOutMs
    2.2F,   // gamma
    false,  // enableLuxGate; temporarily disabled while tuning animation
    15.0F,  // luxThreshold; used only when enableLuxGate is true
    {
        {STRIP_1_START_PIXEL, STRIP_1_PIXEL_COUNT, false},
        {STRIP_2_START_PIXEL, STRIP_2_PIXEL_COUNT, false},
        {STRIP_3_START_PIXEL, STRIP_3_PIXEL_COUNT, false},
        {STRIP_4_START_PIXEL, STRIP_4_PIXEL_COUNT, false},
    },
};
