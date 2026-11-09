#include <array>
#include <cassert>
#include <cstdio>
#include <string>

#include "FadeOutTestTypes.h"
#include "src/led/AnimationEngine.h"
#include "src/sensors/PirSensor.h"

namespace {
using Direction = AnimationEngine::Direction;

LightingConfig configuration(const FadeOutCase& test) {
  return {LIGHTING_CONFIG_VERSION, 5, test.brightness, 100, 0.8F,
          test.startMode, 300, test.durationMs, test.style, test.gamma,
          test.waterfall, test.waterfallDirection, MAX_WATERFALL_SPEED_PPS,
          false, 15.0F,
          {{1, false}, {17, true}, {53, false}, {54, true},
           {static_cast<uint16_t>(test.fullBuffer ? MAX_LOGICAL_PIXELS - 125 : 56), false}},
          0, test.transitionMs, test.fadeOutDirection, test.reverseFadeOutOrder};
}

uint8_t level(const CRGB* pixels, const uint16_t index) {
  const CRGB& pixel = pixels[index / 3];
  return index % 3 == 0 ? pixel.g : index % 3 == 1 ? pixel.r : pixel.b;
}

void uninterruptedFade(const FadeOutCase& test) {
  const auto config = configuration(test);
  assert(validateLightingConfig(config, MAX_LOGICAL_PIXELS).valid());
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT + 1> pixels{};
  pixels.back() = CRGB(11, 22, 33);
  FastLED.captureFrames = true;
  LedController leds(pixels.data(), hardware::WS2811_CONTROLLER_COUNT, MAX_LOGICAL_PIXELS);
  leds.begin(config.maxBrightness, config.gamma);
  assert(FastLED.ditherMode == 0);
  AnimationEngine animation(leds, config);
  animation.begin(test.startMs);
  animation.trigger(Direction::LEFT_TO_RIGHT, test.startMs + 1);
  // Finish even a full-length waterfall before starting the hold countdown.
  animation.update(test.startMs + 20000, false);
  const uint32_t fadeStart = test.startMs + 20300;
  const size_t onFrameCount = FastLED.showCount;
  animation.update(fadeStart, false);
  assert(FastLED.showCount == onFrameCount);
  Serial.clear();

  const uint16_t configured = configuredPixelCount(config);
  auto previous = FastLED.transmitted;
  for (uint32_t elapsed = 1; elapsed <= test.durationMs + 1; ++elapsed) {
    const auto before = FastLED.showCount;
    animation.update(fadeStart + elapsed, test.continuousHigh);
    assert(FastLED.showCount - before <= 1);
    for (uint16_t index = 0; index < MAX_LOGICAL_PIXELS; ++index) {
      const auto current = level(FastLED.transmitted.data(), index);
      assert(current <= level(previous.data(), index));
      if (index >= configured) assert(current == 0);
      if (index < configured && test.style == FadeOutStyle::GLOBAL) {
        assert(current == level(FastLED.transmitted.data(), 0));
      }
    }
    previous = FastLED.transmitted;
    const auto count = FastLED.showCount;
    animation.update(fadeStart + elapsed, test.continuousHigh);
    assert(FastLED.showCount == count);  // Same millis: no duplicate frame.
  }
  assert(animation.isOff());
  for (uint16_t index = 0; index < MAX_LOGICAL_PIXELS; ++index) {
    assert(level(FastLED.transmitted.data(), index) == 0);
  }
  assert(Serial.output().find("FADING_OUT -> OFF") != std::string::npos);
  assert(Serial.output().find("LED OFF frame: bufferMax=0") != std::string::npos);
  assert(Serial.output().find("FADING_OUT -> FADING_IN") == std::string::npos);
  assert(pixels.back().r == 11 && pixels.back().g == 22 && pixels.back().b == 33);
  const auto offFrameCount = FastLED.showCount;
  for (uint32_t elapsed = test.durationMs + 2; elapsed <= test.durationMs + 1000; elapsed += 17) {
    animation.update(fadeStart + elapsed, false);
    assert(animation.isOff());
    assert(FastLED.showCount == offFrameCount);
    assert(leds.maxBufferLevel() == 0);
  }
  FastLED.captureFrames = false;
}

void smoothReversal(const FadeOutCase& test) {
  const auto config = configuration(test);
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> pixels{};
  FastLED.captureFrames = true;
  LedController leds(pixels.data(), hardware::WS2811_CONTROLLER_COUNT, MAX_LOGICAL_PIXELS);
  leds.begin(config.maxBrightness, config.gamma);
  AnimationEngine animation(leds, config);
  animation.begin(0);
  animation.trigger(Direction::LEFT_TO_RIGHT, 1);
  animation.update(20000, false);
  animation.update(20300, false);
  animation.update(20300 + config.fadeOutMs / 4, false);
  const auto displayed = FastLED.transmitted;
  // A delayed loop receives the new edge before drawing another fade-out
  // frame. Reversal must start from what the user last actually saw.
  const uint32_t triggerAt = 20300 + config.fadeOutMs / 2;
  Serial.clear();
  animation.trigger(Direction::RIGHT_TO_LEFT, triggerAt);
  animation.update(triggerAt, true);
  for (uint16_t index = 0; index < configuredPixelCount(config); ++index) {
    assert(level(FastLED.transmitted.data(), index) == level(displayed.data(), index));
  }
  assert(Serial.output().find("FADING_OUT -> FADING_IN") != std::string::npos);
  auto previous = FastLED.transmitted;
  for (uint32_t elapsed = 1; elapsed <= 15000; elapsed += 17) {
    animation.update(triggerAt + elapsed, true);
    for (uint16_t index = 0; index < configuredPixelCount(config); ++index) {
      assert(level(FastLED.transmitted.data(), index) >= level(previous.data(), index));
    }
    previous = FastLED.transmitted;
  }
  FastLED.captureFrames = false;
}

void pirEdges() {
  testPinLevels[5] = LOW;
  PirSensor pir(5, 80, 2000);
  pir.begin(0);
  testPinLevels[5] = HIGH;
  assert(!pir.update(100));
  assert(pir.update(180));
  Serial.clear();
  for (uint32_t now = 181; now < 10000; ++now) {
    assert(!pir.update(now));
    assert(pir.isActive());
  }
  assert(Serial.output().empty());  // No per-frame HIGH logs or triggers.
  testPinLevels[5] = LOW;
  assert(!pir.update(10000));
  assert(!pir.update(10080));
  // A short bounce below the debounce duration must not reverse a fade.
  testPinLevels[5] = HIGH;
  assert(!pir.update(10100));
  testPinLevels[5] = LOW;
  assert(!pir.update(10120));
  assert(!pir.update(10200));
  testPinLevels[5] = HIGH;
  assert(!pir.update(10300));
  assert(pir.update(10380));
  assert(!pir.update(20000));
}

void pirAnimationFlow() {
  const FadeOutCase test{false, 80, 2640, 2.2F, FadeOutStyle::GLOBAL,
                         StripStartMode::CASCADE, WaterfallDirection::TOP_TO_BOTTOM,
                         false, 0, false};
  const auto config = configuration(test);
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> pixels{};
  FastLED.captureFrames = true;
  LedController leds(pixels.data(), hardware::WS2811_CONTROLLER_COUNT, MAX_LOGICAL_PIXELS);
  leds.begin(config.maxBrightness, config.gamma);
  AnimationEngine animation(leds, config);
  animation.begin(0);
  testPinLevels[5] = LOW;
  PirSensor pir(5, 80, 2000);
  pir.begin(0);
  const auto poll = [&](const uint32_t now) {
    if (pir.update(now)) animation.trigger(Direction::LEFT_TO_RIGHT, now);
    animation.update(now, pir.isActive());
  };
  testPinLevels[5] = HIGH;
  poll(100);
  poll(180);
  poll(20000);
  poll(21000);
  poll(22000);
  assert(level(FastLED.transmitted.data(), 0) == config.maxBrightness);
  testPinLevels[5] = LOW;
  poll(22100);
  poll(22180);
  poll(22400);
  poll(22450);
  const auto beforeMotion = FastLED.transmitted;
  assert(level(beforeMotion.data(), 0) < config.maxBrightness);
  Serial.clear();
  testPinLevels[5] = HIGH;
  poll(22500);
  // The last actual frame before the accepted, debounced edge is the baseline.
  const auto lastFrame = FastLED.transmitted;
  poll(22580);
  assert(level(FastLED.transmitted.data(), 0) == level(lastFrame.data(), 0));
  for (uint32_t now = 22581; now < 30000; now += 17) poll(now);
  assert(level(FastLED.transmitted.data(), 0) == config.maxBrightness);
  const std::string transition = "FADING_OUT -> FADING_IN";
  const auto first = Serial.output().find(transition);
  assert(first != std::string::npos);
  assert(Serial.output().find(transition, first + 1) == std::string::npos);
  assert(Serial.output().find("ON -> FADING_OUT") == std::string::npos);
  FastLED.captureFrames = false;
}
}  // namespace

int main() {
  size_t cases = 0;
  for (const bool waterfall : {false, true}) {
    for (const uint8_t brightness : {20, 80, 255}) {
      for (const uint32_t duration : {1000, 2640, 5000}) {
        for (const float gamma : {0.1F, 1.0F, 2.2F, 5.0F}) {
          for (const auto style : {FadeOutStyle::GLOBAL, FadeOutStyle::CASCADE, FadeOutStyle::WATERFALL}) {
            for (const bool high : {false, true}) {
              uninterruptedFade({waterfall, brightness, duration, gamma, style,
                                 StripStartMode::CASCADE, WaterfallDirection::TOP_TO_BOTTOM,
                                 high, 0, false});
              ++cases;
            }
          }
        }
      }
    }
  }
  for (const bool waterfall : {false, true}) {
    const FadeOutCase test{waterfall, 80, 2640, 2.2F, FadeOutStyle::GLOBAL,
                           StripStartMode::SIMULTANEOUS, WaterfallDirection::BOTTOM_TO_TOP,
                           true, UINT32_MAX - 21000, true};
    uninterruptedFade(test);
    smoothReversal(test);
    ++cases;
  }
  pirEdges();
  for (const bool waterfall : {false, true}) {
    for (const uint32_t transition : {0, 600, 3000}) {
      for (const bool reverse : {false, true}) {
        const FadeOutCase test{waterfall, 80, 2640, 2.2F, FadeOutStyle::WATERFALL,
                               StripStartMode::CASCADE, WaterfallDirection::BOTTOM_TO_TOP,
                               true, UINT32_MAX - 21000, true, transition,
                               WaterfallDirection::BOTTOM_TO_TOP, reverse};
        uninterruptedFade(test);
        smoothReversal(test);
        ++cases;
      }
    }
  }
  pirAnimationFlow();
  std::printf("Fade-out: %zu monotonic cases, reversal, frame ownership and PIR edge tests passed\n", cases);
}
