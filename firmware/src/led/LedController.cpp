#include "LedController.h"

#include <math.h>

LedController::LedController(CRGB* leds, const uint16_t controllerCount,
                             const uint16_t logicalPixelCount)
    : leds_(leds),
      controllerCount_(controllerCount),
      logicalPixelCount_(logicalPixelCount) {}

void LedController::begin(const uint8_t maxBrightness, const float gamma) {
  gamma_ = gamma;
  FastLED.addLeds<WS2811, hardware::DATA_PIN, GRB>(leds_, controllerCount_);
  FastLED.setBrightness(maxBrightness);
  clear();
  show();
}

void LedController::clear() {
  fill_solid(leds_, controllerCount_, CRGB::Black);
}

void LedController::show() { FastLED.show(); }

void LedController::setAll(const uint8_t linearLevel) {
  const uint8_t correctedLevel = gammaCorrect(linearLevel);
  fill_solid(leds_, controllerCount_,
             CRGB(correctedLevel, correctedLevel, correctedLevel));
}

void LedController::setStrip(const StripConfig& strip,
                             const uint8_t linearLevel) {
  const uint8_t correctedLevel = gammaCorrect(linearLevel);
  for (uint16_t localPixel = 0; localPixel < strip.pixelCount; ++localPixel) {
    const uint16_t offset =
        strip.reversed ? strip.pixelCount - 1 - localPixel : localPixel;
    const uint16_t logicalPixel = strip.startPixel + offset;
    if (logicalPixel >= logicalPixelCount_) {
      continue;
    }
    setLogicalPixel(logicalPixel, correctedLevel);
  }
}

void LedController::setLogicalPixel(const uint16_t logicalPixel,
                                    const uint8_t correctedLevel) {
  const uint16_t controller = logicalPixel / 3;
  if (controller >= controllerCount_) {
    return;
  }

  // FastLED transmits CRGB fields in the configured GRB wire order. The
  // monochrome WS2811 strip exposes those three bytes as consecutive sections.
  switch (logicalPixel % 3) {
    case 0:
      leds_[controller].g = correctedLevel;
      return;
    case 1:
      leds_[controller].r = correctedLevel;
      return;
    case 2:
      leds_[controller].b = correctedLevel;
      return;
  }
}

uint8_t LedController::gammaCorrect(const uint8_t linearLevel) const {
  const float normalized = static_cast<float>(linearLevel) / 255.0F;
  return static_cast<uint8_t>(powf(normalized, gamma_) * 255.0F + 0.5F);
}
