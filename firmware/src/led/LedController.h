#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "../../config.h"

class LedController {
 public:
  LedController(CRGB* leds, uint16_t controllerCount,
                uint16_t logicalPixelCount);

  void begin(uint8_t maxBrightness, float gamma);
  void configure(uint8_t maxBrightness, float gamma);
  void clear();
  void show();
  void setAll(uint8_t linearLevel);
  void setRange(uint16_t startPixel, uint16_t pixelCount,
                uint8_t linearLevel);
  void setLogicalPixel(uint16_t logicalPixel, uint8_t linearLevel);

 private:
  uint8_t gammaCorrect(uint8_t linearLevel) const;
  void setCorrectedLogicalPixel(uint16_t logicalPixel,
                                uint8_t correctedLevel);

  CRGB* const leds_;
  const uint16_t controllerCount_;
  const uint16_t logicalPixelCount_;
  float gamma_ = 2.2F;
};
