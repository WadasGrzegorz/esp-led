#pragma once

#include <Arduino.h>

#include "../../config.h"
#include "LedController.h"

class AnimationEngine {
 public:
  enum class Direction : uint8_t {
    LEFT_TO_RIGHT,
    RIGHT_TO_LEFT,
  };

  AnimationEngine(LedController& leds, const LightingConfig& config);

  void begin(uint32_t nowMs);
  void update(uint32_t nowMs);
  void trigger(Direction direction, uint32_t nowMs);
  bool isOff() const;

 private:
  enum class State : uint8_t {
    OFF,
    FADING_IN,
    ON,
    FADING_OUT,
  };

  void startCascade(Direction direction, uint32_t nowMs);
  void startFadeOut(uint32_t nowMs);
  void enterOn(uint32_t nowMs);
  void enterOff(uint32_t nowMs);
  void renderFadeIn(uint32_t elapsedMs);
  void renderFadeOut(uint32_t elapsedMs);
  void updateFadeOutLevels(uint32_t elapsedMs);
  void renderLevels();
  void logNewlyStartedStrips(uint32_t elapsedMs);
  uint8_t stripIndexAt(uint8_t orderPosition) const;
  static uint32_t scaledDuration(uint32_t fullDurationMs,
                                 uint8_t levelDistance);
  static uint8_t fadeLevel(uint32_t elapsedMs, uint32_t durationMs);
  static const char* directionName(Direction direction);
  static const char* cascadeName(Direction direction);

  LedController& leds_;
  const LightingConfig& config_;
  State state_ = State::OFF;
  Direction direction_ = Direction::LEFT_TO_RIGHT;
  uint32_t stateStartedAtMs_ = 0;
  uint32_t lastRenderedAtMs_ = UINT32_MAX;
  uint32_t cascadeDurationMs_ = 0;
  uint8_t stripLevels_[hardware::STRIP_COUNT] = {};
  uint8_t fadeInStartLevels_[hardware::STRIP_COUNT] = {};
  uint8_t fadeOutStartLevels_[hardware::STRIP_COUNT] = {};
  uint32_t stripFadeInStartedAtMs_[hardware::STRIP_COUNT] = {};
  uint32_t stripFadeInDurationsMs_[hardware::STRIP_COUNT] = {};
  bool stripStartLogged_[hardware::STRIP_COUNT] = {};
};
