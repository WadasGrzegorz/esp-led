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
  bool selectStripForTest(uint8_t stripNumber);
  void update(uint32_t nowMs, bool motionActive);
  void trigger(Direction direction, uint32_t nowMs);
  bool isOff() const;

 private:
  enum class State : uint8_t {
    OFF,
    WAITING_TO_START,
    FADING_IN,
    ON,
    FADING_OUT,
  };

  void startCascade(Direction direction, uint32_t nowMs);
  void startFadeOut(uint32_t nowMs);
  void enterOn(uint32_t nowMs);
  void enterOff(uint32_t nowMs);
  void renderFadeIn(uint32_t elapsedMs);
  void renderWaterfallFadeIn(uint32_t elapsedMs);
  void renderFadeOut(uint32_t elapsedMs);
  void prepareWaterfallFadeOut();
  void renderWaterfallFadeOut(uint32_t elapsedMs);
  uint8_t waterfallFadeOutPixelLevel(uint8_t stripIndex, uint16_t logicalOffset,
                                     uint32_t elapsedMs) const;
  uint8_t waterfallFadeLevel(uint32_t elapsedMs, uint32_t durationMs) const;
  void updateFadeOutLevels(uint32_t elapsedMs);
  void renderLevels();
  void logNewlyStartedStrips(uint32_t elapsedMs);
  uint8_t stripIndexAt(uint8_t orderPosition) const;
  uint8_t fadeOutStripIndexAt(uint8_t orderPosition) const;
  bool isStripVisible(uint8_t stripIndex) const;
  uint32_t waterfallPixelStepMs() const;
  uint32_t waterfallPixelFadeMs() const;
  uint32_t waterfallStripDurationMs(uint16_t pixelCount) const;
  static uint32_t scaledDuration(uint32_t fullDurationMs,
                                 uint8_t levelDistance);
  static uint8_t fadeLevel(uint32_t elapsedMs, uint32_t durationMs);
  static const char* directionName(Direction direction);
  void printCascadeName() const;
  void transitionTo(State nextState, uint32_t nowMs, const char* reason);
  static const char* stateName(State state);

  LedController& leds_;
  const LightingConfig& config_;
  State state_ = State::OFF;
  Direction direction_ = Direction::LEFT_TO_RIGHT;
  uint32_t stateStartedAtMs_ = 0;
  uint32_t lastRenderedAtMs_ = UINT32_MAX;
  uint32_t cascadeDurationMs_ = 0;
  uint8_t testStripNumber_ = 0;
  Direction fadeOutDirection_ = Direction::LEFT_TO_RIGHT;
  bool resumeWaterfallFadeOut_ = false;
  uint32_t lastFadeOutElapsedMs_ = 0;
  uint8_t stripLevels_[MAX_STRIPS] = {};
  uint8_t fadeInStartLevels_[MAX_STRIPS] = {};
  uint8_t fadeOutStartLevels_[MAX_STRIPS] = {};
  uint32_t stripFadeInStartedAtMs_[MAX_STRIPS] = {};
  uint32_t stripFadeInDurationsMs_[MAX_STRIPS] = {};
  uint32_t stripFadeOutStartedAtMs_[MAX_STRIPS] = {};
  uint32_t stripFadeOutDurationsMs_[MAX_STRIPS] = {};
  uint32_t stripFadeOutPixelFadeMs_[MAX_STRIPS] = {};
  bool stripStartLogged_[MAX_STRIPS] = {};
};
