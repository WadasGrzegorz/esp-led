#include "LedController.h"

#include <math.h>

#include "../diagnostics/Diagnostics.h"

LedController::LedController(CRGB* leds, const uint16_t controllerCount,
                             const uint16_t logicalPixelCount)
    : leds_(leds),
      controllerCount_(controllerCount),
      logicalPixelCount_(logicalPixelCount) {}

void LedController::begin(const uint8_t maxBrightness, const float gamma) {
  FastLED.addLeds<WS2811, hardware::DATA_PIN, GRB>(leds_, controllerCount_);
  configure(maxBrightness, gamma);
  clear();
  show();
}

void LedController::configure(const uint8_t maxBrightness, const float gamma) {
  gamma_ = gamma;
  // Temporal dithering can turn a low output back up on the next frame.
  // Monochrome fades require deterministic, monotonic output at every FPS.
  FastLED.setDither(0);
  FastLED.setBrightness(maxBrightness);
}

void LedController::clear() {
  fill_solid(leds_, controllerCount_, CRGB::Black);
}

void LedController::show() {
  diagnostics.prepareOutput({leds_, controllerCount_, FastLED.getBrightness()});
  const uint32_t startedUs = micros();
  FastLED.show();
  const uint32_t transmitUs = micros() - startedUs;
  diagnostics.completeOutput();
  diagnostics.frame({leds_, controllerCount_, FastLED.getBrightness(),
                     FastLED[0].getDither() != 0, millis(), transmitUs});
}

uint8_t LedController::maxBufferLevel() const {
  uint8_t maximum = 0;
  for (uint16_t index = 0; index < controllerCount_; ++index) {
    maximum = max(maximum, max(leds_[index].r, max(leds_[index].g, leds_[index].b)));
  }
  return maximum;
}

void LedController::setAll(const uint8_t linearLevel) {
  const uint8_t correctedLevel = gammaCorrect(linearLevel);
  fill_solid(leds_, controllerCount_,
             CRGB(correctedLevel, correctedLevel, correctedLevel));
}

void LedController::setRange(const uint16_t startPixel,
                             const uint16_t pixelCount,
                             const uint8_t linearLevel) {
  const uint8_t correctedLevel = gammaCorrect(linearLevel);
  if (startPixel >= logicalPixelCount_) {
    if (pixelCount != 0) diagnostics.invalidWrite({startPixel, pixelCount, logicalPixelCount_});
    return;
  }
  if (pixelCount > logicalPixelCount_ - startPixel) {
    diagnostics.invalidWrite({startPixel, pixelCount, logicalPixelCount_});
  }
  // Clip once to the remaining capacity so every loop offset stays in bounds.
  const uint16_t boundedCount =
      min(pixelCount, static_cast<uint16_t>(logicalPixelCount_ - startPixel));
  for (uint16_t offset = 0; offset < boundedCount; ++offset) {
    setCorrectedLogicalPixel(startPixel + offset, correctedLevel);
  }
}

void LedController::setLogicalPixel(const uint16_t logicalPixel,
                                    const uint8_t linearLevel) {
  if (logicalPixel >= logicalPixelCount_) {
    diagnostics.invalidWrite({logicalPixel, 1, logicalPixelCount_});
    return;
  }
  setCorrectedLogicalPixel(logicalPixel, gammaCorrect(linearLevel));
}

void LedController::setCorrectedLogicalPixel(const uint16_t logicalPixel,
                                             const uint8_t correctedLevel) {
  const uint16_t controller =
      logicalPixel / hardware::WS2811_SECTIONS_PER_CONTROLLER;
  if (controller >= controllerCount_) {
    return;
  }

  // FastLED transmits CRGB fields in the configured GRB wire order. The
  // monochrome WS2811 strip exposes those three bytes as consecutive sections.
  switch (logicalPixel % hardware::WS2811_SECTIONS_PER_CONTROLLER) {
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
