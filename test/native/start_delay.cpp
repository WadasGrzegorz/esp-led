#include <cassert>
#include <cstdio>

#include <Preferences.h>

#include "src/config/ConfigStorageTypes.h"
#include "src/config/ConfigurationManager.h"
#include "src/led/AnimationEngine.h"

namespace {
using Direction = AnimationEngine::Direction;

constexpr LightingConfig testConfig(const uint32_t delayMs) {
  return {LIGHTING_CONFIG_VERSION, 2, 80, 100, 0.8F,
          StripStartMode::CASCADE, 300, 200, FadeOutStyle::GLOBAL, 1.0F,
          false, WaterfallDirection::TOP_TO_BOTTOM, 40, false, 15.0F,
          {{3, false}, {3, false}}, delayMs};
}

void delayedStart(const Direction direction) {
  const LightingConfig config = testConfig(2000);
  CRGB pixels[2];
  LedController leds(pixels, 2, 6);
  AnimationEngine animation(leds, config);
  animation.begin(100);
  animation.trigger(direction, 100);
  assert(!animation.isOff());

  // Opposite motion must not change the original deadline or direction.
  animation.trigger(direction == Direction::LEFT_TO_RIGHT
                        ? Direction::RIGHT_TO_LEFT
                        : Direction::LEFT_TO_RIGHT,
                    1500);
  animation.update(2099, false);
  assert(pixels[0].g == 0 && pixels[1].g == 0);
  animation.update(2100, false);
  animation.update(2150, false);
  const uint8_t first = direction == Direction::LEFT_TO_RIGHT ? 0 : 1;
  assert(pixels[first].g > 0 && pixels[1 - first].g == 0);
  animation.update(2280, false);
  assert(pixels[0].g == 255 && pixels[1].g == 255);
  animation.update(2579, false);
  assert(pixels[0].g == 255 && pixels[1].g == 255);
  animation.update(2580, false);
  animation.update(2680, false);
  assert(pixels[0].g > 0 && pixels[0].g < 255);

  // Reversing fade-out must remain immediate despite the configured delay.
  animation.trigger(direction, 2680);
  animation.update(2705, true);
  assert(pixels[first].g > 128);
  animation.update(3000, true);
  animation.update(4000, true);
  assert(pixels[0].g == 255 && pixels[1].g == 255);
  animation.update(4300, false);
  animation.update(4500, false);
  assert(animation.isOff());

  // A new cycle waits again, and begin() cancels a pending start.
  animation.trigger(direction, 5000);
  animation.update(6999, false);
  assert(pixels[0].g == 0 && pixels[1].g == 0);
  animation.begin(6999);
  animation.update(9000, true);
  assert(animation.isOff());
  assert(pixels[0].g == 0 && pixels[1].g == 0);
}

void immediateStart() {
  const LightingConfig config = testConfig(0);
  CRGB pixels[2];
  LedController leds(pixels, 2, 6);
  AnimationEngine animation(leds, config);
  animation.begin(0);
  animation.trigger(Direction::LEFT_TO_RIGHT, 100);
  animation.update(150, false);
  assert(pixels[0].g > 0 && pixels[1].g == 0);
}

void millisRollover() {
  const LightingConfig config = testConfig(2000);
  CRGB pixels[2];
  LedController leds(pixels, 2, 6);
  AnimationEngine animation(leds, config);
  const uint32_t start = UINT32_MAX - 1000;
  animation.begin(start);
  animation.trigger(Direction::LEFT_TO_RIGHT, start);
  animation.update(start + 1999U, true);
  assert(pixels[0].g == 0);
  animation.update(start + 2000U, true);
  animation.update(start + 2050U, true);
  assert(pixels[0].g > 0 && pixels[1].g == 0);
}

void lateUpdateKeepsFullAnimation() {
  const LightingConfig config = testConfig(2000);
  CRGB pixels[2];
  LedController leds(pixels, 2, 6);
  AnimationEngine animation(leds, config);
  animation.begin(0);
  animation.trigger(Direction::LEFT_TO_RIGHT, 100);
  animation.update(9000, false);
  assert(pixels[0].g == 0 && pixels[1].g == 0);
  animation.update(9050, false);
  assert(pixels[0].g > 0 && pixels[0].g < 255 && pixels[1].g == 0);
}

template <typename Legacy>
void migratesWithZeroDelay(const Legacy& legacy) {
  Preferences preferences;
  preferences.putBytes("config", &legacy, sizeof(legacy));
  ConfigurationManager configuration(DEFAULT_LIGHTING_CONFIG, MAX_LOGICAL_PIXELS);
  configuration.begin();
  const LightingConfig& migrated = configuration.get();
  assert(migrated.configVersion == LIGHTING_CONFIG_VERSION);
  assert(migrated.startDelayMs == 0);
  assert(migrated.stripCount == 2);
  assert(migrated.maxBrightness == 90);
  assert(migrated.holdMs == 700);
  assert(migrated.strips[0].pixelCount == 5);
  assert(migrated.strips[1].pixelCount == 7);
  assert(migrated.strips[1].reversed);
  // Migration is in RAM until explicitly saved.
  assert(preferences.getBytesLength("config") == sizeof(legacy));
  assert(configuration.save());
  ConfigurationManager reloaded(DEFAULT_LIGHTING_CONFIG, MAX_LOGICAL_PIXELS);
  reloaded.begin();
  assert(reloaded.get().stripCount == 2 && reloaded.get().startDelayMs == 0);
  if constexpr (std::is_same_v<Legacy, LegacyLightingConfigV4>) {
    assert(migrated.stripStartMode == legacy.stripStartMode);
    assert(migrated.fadeOutStyle == legacy.fadeOutStyle);
    assert(migrated.waterfallSpeedPps == legacy.waterfallSpeedPps);
    assert(migrated.waterfallDirection == legacy.waterfallDirection);
    assert(migrated.enableLuxGate == legacy.enableLuxGate);
    assert(migrated.luxThreshold == legacy.luxThreshold);
  }
}

void configurationPersistence() {
  migratesWithZeroDelay(LegacyLightingConfigV1{
      1, 2, 90, 100, 0.8F, 700, 200, 1.0F, false, 15.0F,
      {{5, false}, {7, true}}});
  migratesWithZeroDelay(LegacyLightingConfigV2{
      2, 2, 90, 100, 0.8F, 700, 200, 1.0F, false,
      WaterfallDirection::BOTTOM_TO_TOP, false, 15.0F,
      {{5, false}, {7, true}}});
  migratesWithZeroDelay(LegacyLightingConfigV3{
      3, 2, 90, 100, 0.8F, 700, 200, 1.0F, false,
      WaterfallDirection::BOTTOM_TO_TOP, 60, false, 15.0F,
      {{5, false}, {7, true}}});
  migratesWithZeroDelay(LegacyLightingConfigV4{
      4, 2, 90, 100, 0.8F, StripStartMode::SIMULTANEOUS, 700, 200,
      FadeOutStyle::CASCADE, 1.0F, false, WaterfallDirection::BOTTOM_TO_TOP,
      60, true, 25.0F, {{5, false}, {7, true}}});

  ConfigurationManager configuration(DEFAULT_LIGHTING_CONFIG, MAX_LOGICAL_PIXELS);
  assert(configuration.update(testConfig(2000)).valid());
  assert(configuration.save());
  ConfigurationManager reloaded(DEFAULT_LIGHTING_CONFIG, MAX_LOGICAL_PIXELS);
  reloaded.begin();
  assert(reloaded.get().startDelayMs == 2000);
  assert(reloaded.get().stripCount == 2);
  assert(reloaded.restoreFactoryDefaults().valid());
  assert(reloaded.get().startDelayMs == 0);
}
}  // namespace

int main() {
  assert(validateLightingConfig(testConfig(0), MAX_LOGICAL_PIXELS).valid());
  assert(validateLightingConfig(testConfig(MAX_START_DELAY_MS), MAX_LOGICAL_PIXELS).valid());
  assert(validateLightingConfig(testConfig(MAX_START_DELAY_MS + 1), MAX_LOGICAL_PIXELS).error ==
         ConfigValidationError::START_DELAY);
  assert(validateLightingConfig(testConfig(UINT32_MAX), MAX_LOGICAL_PIXELS).error ==
         ConfigValidationError::START_DELAY);
  delayedStart(Direction::LEFT_TO_RIGHT);
  delayedStart(Direction::RIGHT_TO_LEFT);
  immediateStart();
  millisRollover();
  lateUpdateKeepsFullAnimation();
  configurationPersistence();
  std::puts("Start delay: animation, timing, validation and NVS tests passed");
}
