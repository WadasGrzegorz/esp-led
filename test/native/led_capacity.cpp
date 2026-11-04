#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>

#include <Preferences.h>

#include "src/config/ConfigurationManager.h"
#include "src/led/AnimationEngine.h"
#include "src/setup/SetupController.h"

namespace {
using Direction = AnimationEngine::Direction;

// Build immutable configurations with nonempty, individually reversed strips.
template <bool Waterfall = false,
          StripStartMode StartMode = StripStartMode::CASCADE,
          FadeOutStyle FadeStyle = FadeOutStyle::GLOBAL,
          WaterfallDirection FallDirection = WaterfallDirection::TOP_TO_BOTTOM,
          size_t... Indices>
constexpr LightingConfig geometry(const uint8_t stripCount,
                                  const uint16_t total,
                                  std::index_sequence<Indices...>) {
  return {LIGHTING_CONFIG_VERSION, stripCount, 80, 100, 0.8F,
          StartMode, 300, 200, FadeStyle, 1.0F, Waterfall, FallDirection,
          MAX_WATERFALL_SPEED_PPS, false, 15.0F,
          {{static_cast<uint16_t>(Indices < stripCount
                                     ? total / stripCount +
                                           (Indices < total % stripCount)
                                     : 0),
            Indices % 2 != 0}...},
          0};
}

constexpr auto stripIndices = std::make_index_sequence<MAX_STRIPS>{};

uint8_t logicalLevel(const CRGB* pixels, const uint16_t index) {
  const CRGB& controller = pixels[index / 3];
  return index % 3 == 0 ? controller.g
                       : index % 3 == 1 ? controller.r : controller.b;
}

void geometryValidation() {
  ConfigurationManager configuration(DEFAULT_LIGHTING_CONFIG,
                                     MAX_LOGICAL_PIXELS);
  for (const auto& config : {
           geometry(4, 180, stripIndices), geometry(5, 181, stripIndices),
           geometry(5, 270, stripIndices), geometry(8, 360, stripIndices),
           geometry(8, 361, stripIndices),
           geometry(8, MAX_LOGICAL_PIXELS, stripIndices),
           geometry(1, MAX_LOGICAL_PIXELS, stripIndices),
           geometry(MAX_STRIPS, MAX_LOGICAL_PIXELS, stripIndices),
           geometry(1, 1, stripIndices)}) {
    const auto validation = configuration.update(config);
    assert(validation.valid());
    assert(validation.totalPixelCount == configuredPixelCount(config));
    assert(configuration.get().stripCount == config.stripCount);
    assert(configuredPixelCount(configuration.get()) ==
           configuredPixelCount(config));
  }
  const LightingConfig previous = configuration.get();
  for (const auto& config : {
           geometry(8, MAX_LOGICAL_PIXELS + 1, stripIndices),
           geometry(1, UINT16_MAX, stripIndices)}) {
    assert(configuration.update(config).error == ConfigValidationError::PIXEL_LIMIT);
    assert(configuredPixelCount(configuration.get()) == configuredPixelCount(previous));
  }
  assert(configuration.update(geometry(1, 0, stripIndices)).error ==
         ConfigValidationError::EMPTY_STRIP);
  assert(configuration.update(geometry(MAX_STRIPS + 1, 180, stripIndices)).error ==
         ConfigValidationError::STRIP_COUNT);
  assert(configuration.update(geometry(0, 0, stripIndices)).error ==
         ConfigValidationError::STRIP_COUNT);
}

void unchangedNvs() {
  // begin() must load existing bytes without rewriting even configurations
  // that were previously above the old prototype limit.
  for (const auto& config : {DEFAULT_LIGHTING_CONFIG,
                             geometry(5, 181, stripIndices),
                             geometry(8, MAX_LOGICAL_PIXELS, stripIndices)}) {
    Preferences preferences;
    preferences.putBytes("config", &config, sizeof(config));
    ConfigurationManager configuration(DEFAULT_LIGHTING_CONFIG,
                                       MAX_LOGICAL_PIXELS);
    configuration.begin();
    assert(std::memcmp(&configuration.get(), &config, sizeof(config)) == 0);
    const auto stored = [&preferences] {
      std::array<uint8_t, sizeof(LightingConfig)> bytes{};
      assert(preferences.getBytes("config", bytes.data(), bytes.size()) == bytes.size());
      return bytes;
    }();
    assert(preferences.getBytesLength("config") == sizeof(config));
    assert(std::memcmp(stored.data(), &config, sizeof(config)) == 0);
  }
}

void addressing() {
  // Guard pixels detect writes on either side of the actual CRGB buffer.
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT + 2> pixels{};
  pixels.front() = CRGB(11, 22, 33);
  pixels.back() = CRGB(44, 55, 66);
  LedController leds(pixels.data() + 1, hardware::WS2811_CONTROLLER_COUNT,
                     MAX_LOGICAL_PIXELS);
  leds.begin(80, 1.0F);
  assert(FastLED.controllerCount == hardware::WS2811_CONTROLLER_COUNT);
  assert(sizeof(CRGB) * hardware::WS2811_CONTROLLER_COUNT == MAX_LOGICAL_PIXELS);
  for (uint16_t index = 0; index < MAX_LOGICAL_PIXELS; ++index) {
    leds.clear();
    leds.setLogicalPixel(index, 123);
    const auto* bytes = reinterpret_cast<const uint8_t*>(pixels.data() + 1);
    const size_t byteCount = sizeof(CRGB) * hardware::WS2811_CONTROLLER_COUNT;
    size_t lit = 0;
    for (size_t byte = 0; byte < byteCount; ++byte) {
      assert(bytes[byte] == 0 || bytes[byte] == 123);
      lit += bytes[byte] == 123;
    }
    assert(lit == 1);
    assert(logicalLevel(pixels.data() + 1, index) == 123);
  }
  assert(pixels[hardware::WS2811_CONTROLLER_COUNT].b == 123);
  leds.clear();
  leds.setRange(MAX_LOGICAL_PIXELS - 1, UINT16_MAX, 255);
  assert(pixels[hardware::WS2811_CONTROLLER_COUNT].b == 255);
  assert(pixels[1].g == 0);
  leds.clear();
  leds.setLogicalPixel(MAX_LOGICAL_PIXELS, 255);
  leds.setLogicalPixel(UINT16_MAX, 255);
  leds.setRange(MAX_LOGICAL_PIXELS, 1, 255);
  leds.setRange(UINT16_MAX, UINT16_MAX, 255);
  leds.setRange(0, 0, 255);
  for (size_t index = 1; index <= hardware::WS2811_CONTROLLER_COUNT; ++index) {
    assert(pixels[index].r == 0 && pixels[index].g == 0 && pixels[index].b == 0);
  }
  assert(pixels.front().r == 11 && pixels.front().g == 22 && pixels.front().b == 33);
  assert(pixels.back().r == 44 && pixels.back().g == 55 && pixels.back().b == 66);
}

void animationCycle(const LightingConfig& config, const Direction direction) {
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT + 1> pixels{};
  pixels.back() = CRGB(11, 22, 33);
  LedController leds(pixels.data(), hardware::WS2811_CONTROLLER_COUNT,
                     MAX_LOGICAL_PIXELS);
  leds.configure(config.maxBrightness, config.gamma);
  AnimationEngine animation(leds, config);
  animation.begin(0);
  animation.trigger(direction, 1);
  animation.update(51, true);
  const uint8_t firstStrip =
      direction == Direction::LEFT_TO_RIGHT ? 0 : config.stripCount - 1;
  uint16_t stripStart = 0;
  for (uint8_t index = 0; index < config.stripCount; ++index) {
    const auto& strip = config.strips[index];
    const bool started = config.stripStartMode == StripStartMode::SIMULTANEOUS ||
                         index == firstStrip;
    const bool reverse = strip.reversed !=
                         (config.waterfallDirection == WaterfallDirection::BOTTOM_TO_TOP);
    const uint16_t leadingPixel = stripStart + (reverse ? strip.pixelCount - 1 : 0);
    const uint16_t trailingPixel = stripStart + (reverse ? 0 : strip.pixelCount - 1);
    assert((logicalLevel(pixels.data(), leadingPixel) > 0) == started);
    if (config.enableWaterfall && strip.pixelCount > 8) {
      assert(logicalLevel(pixels.data(), trailingPixel) == 0);
    }
    if (!config.enableWaterfall) {
      assert(logicalLevel(pixels.data(), trailingPixel) ==
             logicalLevel(pixels.data(), leadingPixel));
    }
    stripStart += strip.pixelCount;
  }
  // Advance through actual intermediate frames, including waterfall and fades.
  for (uint32_t now = 52; now < 20000; now += 10) {
    animation.update(now, true);
  }
  const uint16_t configured = configuredPixelCount(config);
  for (uint16_t index = 0; index < MAX_LOGICAL_PIXELS; ++index) {
    assert(logicalLevel(pixels.data(), index) == (index < configured ? 255 : 0));
  }
  assert(pixels.back().r == 11 && pixels.back().g == 22 && pixels.back().b == 33);
  animation.update(20302, false);
  animation.update(20402, false);
  animation.update(20502, false);
  assert(animation.isOff());
  for (uint16_t index = 0; index < MAX_LOGICAL_PIXELS; ++index) {
    assert(logicalLevel(pixels.data(), index) == 0);
  }
}

void animations() {
  for (const auto direction : {Direction::LEFT_TO_RIGHT, Direction::RIGHT_TO_LEFT}) {
    for (const auto& config : {
             geometry(8, MAX_LOGICAL_PIXELS, stripIndices),
             geometry<false, StripStartMode::SIMULTANEOUS, FadeOutStyle::CASCADE>(
                 8, MAX_LOGICAL_PIXELS, stripIndices),
             geometry<true, StripStartMode::CASCADE, FadeOutStyle::CASCADE>(
                 8, MAX_LOGICAL_PIXELS, stripIndices),
             geometry<true, StripStartMode::SIMULTANEOUS, FadeOutStyle::GLOBAL,
                      WaterfallDirection::BOTTOM_TO_TOP>(8, MAX_LOGICAL_PIXELS, stripIndices),
             geometry<true>(5, 181, stripIndices),
             geometry<true>(1, 1, stripIndices)}) {
      animationCycle(config, direction);
    }
  }
}

void calibration() {
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> pixels{};
  LedController leds(pixels.data(), hardware::WS2811_CONTROLLER_COUNT,
                     MAX_LOGICAL_PIXELS);
  ConfigurationManager configuration(DEFAULT_LIGHTING_CONFIG,
                                     MAX_LOGICAL_PIXELS);
  assert(configuration.update(DEFAULT_LIGHTING_CONFIG).valid());
  SetupController setup(leds, configuration, MAX_LOGICAL_PIXELS);
  assert(setup.startCalibration() == SetupResult::OK);
  assert(setup.goToPixel(MAX_LOGICAL_PIXELS - 1) == SetupResult::OK);
  assert(pixels.back().b == 255);
  assert(setup.moveCursor(1) == SetupResult::OUT_OF_RANGE);
  assert(setup.goToPixel(MAX_LOGICAL_PIXELS) == SetupResult::OUT_OF_RANGE);
  assert(setup.finishCalibration() == SetupResult::OK);
  assert(configuration.get().stripCount == 1);
  assert(configuredPixelCount(configuration.get()) == MAX_LOGICAL_PIXELS);
  assert(setup.startCalibration() == SetupResult::OK);
  assert(setup.goToPixel(180) == SetupResult::OK);
  assert(setup.finishCalibration() == SetupResult::OK);
  assert(configuredPixelCount(configuration.get()) == 181);
}
}  // namespace

int main() {
  geometryValidation();
  unchangedNvs();
  addressing();
  animations();
  calibration();
  std::puts("LED capacity: validation, NVS, addressing, animation and calibration tests passed");
}
