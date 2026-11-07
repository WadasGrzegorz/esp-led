#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

#include "src/diagnostics/Diagnostics.h"
#include "src/led/AnimationEngine.h"

namespace {
using Direction = AnimationEngine::Direction;
using Frames = std::vector<std::vector<CRGB>>;

uint8_t section(const std::vector<CRGB>& pixels, const uint16_t index) {
  const auto& pixel = pixels[index / 3];
  return index % 3 == 0 ? pixel.g : index % 3 == 1 ? pixel.r : pixel.b;
}

Frames run(const LightingConfig& config, const uint8_t strip,
           const Direction direction) {
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT + 1> buffer{};
  buffer.back() = CRGB(11, 22, 33);
  FastLED.captureFrames = true;
  LedController leds(buffer.data(), hardware::WS2811_CONTROLLER_COUNT, MAX_LOGICAL_PIXELS);
  leds.begin(config.maxBrightness, config.gamma);
  AnimationEngine animation(leds, config);
  diagnostics.reset({config, "strip-comparison", 0, strip});
  animation.begin(0);
  assert(!animation.selectStripForTest(0));
  assert(!animation.selectStripForTest(config.stripCount + 1));
  if (strip != 0) assert(animation.selectStripForTest(strip));
  animation.trigger(direction, 0);
  assert(!animation.selectStripForTest(1));  // Cannot retarget an active cycle.

  Frames frames;
  for (uint32_t at = 0; at <= 12000; at += 17) {
    animation.update(at, false);
    frames.push_back(FastLED.transmitted);
    assert(buffer.back().r == 11 && buffer.back().g == 22 && buffer.back().b == 33);
  }
  assert(animation.isOff());
  assert(leds.maxBufferLevel() == 0);
  assert(diagnostics.stats().invalidWrites == 0);
  assert(diagnostics.stats().fadeOutIncreaseFrames == 0);

  // Completing a selected cycle must not leave a filter on the next cycle.
  animation.trigger(direction, 13000);
  animation.update(14000, false);
  animation.update(23000, false);
  for (uint16_t pixel = 0; pixel < configuredPixelCount(config); ++pixel) {
    assert(section(FastLED.transmitted, pixel) == config.maxBrightness);
  }
  animation.begin(24000);
  assert(animation.selectStripForTest(config.stripCount));
  animation.begin(24001);  // Stop/reset also removes a pending selection.
  animation.trigger(direction, 24002);
  animation.update(34002, false);
  animation.update(44002, false);
  for (uint16_t pixel = 0; pixel < configuredPixelCount(config); ++pixel) {
    assert(section(FastLED.transmitted, pixel) == config.maxBrightness);
  }
  FastLED.captureFrames = false;
  return frames;
}
}  // namespace

int main() {
  size_t cases = 0;
  for (const uint8_t stripCount : std::array<uint8_t, 2>{5, MAX_STRIPS}) {
    for (const bool waterfall : {false, true}) {
      for (const auto direction : {Direction::LEFT_TO_RIGHT, Direction::RIGHT_TO_LEFT}) {
        for (const auto startMode : {StripStartMode::CASCADE, StripStartMode::SIMULTANEOUS}) {
          for (const auto fadeStyle : {FadeOutStyle::GLOBAL, FadeOutStyle::CASCADE}) {
            LightingConfig config = DEFAULT_LIGHTING_CONFIG;
            config.stripCount = stripCount;
            config.maxBrightness = 80;
            config.stripFadeInMs = 180;
            config.startDelayMs = 35;
            config.holdMs = 80;
            config.fadeOutMs = 350;
            config.enableWaterfall = waterfall;
            config.waterfallSpeedPps = 120;
            config.waterfallDirection = direction == Direction::LEFT_TO_RIGHT
                                            ? WaterfallDirection::TOP_TO_BOTTOM
                                            : WaterfallDirection::BOTTOM_TO_TOP;
            config.stripStartMode = startMode;
            config.fadeOutStyle = fadeStyle;
            if (stripCount == 5) {
              config.strips[0] = {1, false};
              config.strips[1] = {17, true};
              config.strips[2] = {53, false};
              config.strips[3] = {54, true};
              config.strips[4] = {MAX_LOGICAL_PIXELS - 125, true};
            } else {
              for (uint8_t index = 0; index < stripCount; ++index) {
                config.strips[index] = {MAX_LOGICAL_PIXELS / MAX_STRIPS, index % 2 == 0};
              }
            }
            assert(validateLightingConfig(config, MAX_LOGICAL_PIXELS).valid());
            const auto reference = run(config, 0, direction);
            for (const uint8_t strip : {uint8_t{1}, static_cast<uint8_t>(stripCount / 2), stripCount}) {
              const auto isolated = run(config, strip, direction);
              assert(reference.size() == isolated.size());
              uint16_t startPixel = 0;
              for (uint8_t index = 0; index + 1 < strip; ++index) {
                startPixel += config.strips[index].pixelCount;
              }
              const uint16_t endPixel = startPixel + config.strips[strip - 1].pixelCount;
              bool sawLight = false;
              for (size_t frame = 0; frame < reference.size(); ++frame) {
                for (uint16_t pixel = 0; pixel < MAX_LOGICAL_PIXELS; ++pixel) {
                  const bool selected = pixel >= startPixel && pixel < endPixel;
                  const uint8_t expected = selected ? section(reference[frame], pixel) : 0;
                  assert(section(isolated[frame], pixel) == expected);
                  sawLight |= expected != 0;
                }
              }
              assert(sawLight);
              ++cases;
            }
          }
        }
      }
    }
  }
  std::printf("Strip animation: %zu isolated/full-cycle comparisons, physical offsets, last pixel, reset and completion passed\n", cases);
}
