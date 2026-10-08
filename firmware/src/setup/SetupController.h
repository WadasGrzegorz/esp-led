#pragma once

#include <Arduino.h>

#include "../config/ConfigurationManager.h"
#include "../led/LedController.h"

enum class SetupResult : uint8_t {
  OK,
  ALREADY_ACTIVE,
  NOT_ACTIVE,
  OUT_OF_RANGE,
  TOO_MANY_STRIPS,
  NOTHING_TO_UNDO,
  INVALID_CONFIG,
};

class SetupController {
 public:
  SetupController(LedController& leds, ConfigurationManager& configuration,
                  uint16_t logicalPixelLimit);

  SetupResult startCalibration();
  SetupResult moveCursor(int16_t delta);
  SetupResult goToPixel(uint16_t logicalPixel);
  SetupResult markBoundary();
  SetupResult undoBoundary();
  SetupResult finishCalibration();
  SetupResult cancelCalibration();

  SetupResult testStrip(uint8_t stripNumber);
  SetupResult stopStripTest();
  void refreshDisplay();

  bool isCalibrationActive() const;
  bool isStripTestActive() const;
  bool isServiceModeActive() const;
  uint16_t cursor() const;
  uint16_t currentStripStart() const;
  uint8_t markedBoundaryCount() const;
  uint16_t markedBoundary(uint8_t index) const;
  ConfigValidationResult lastValidation() const;

 private:
  void renderCalibration();
  void renderStripTest();
  uint16_t stripStartPixel(uint8_t stripIndex) const;

  LedController& leds_;
  ConfigurationManager& configuration_;
  const uint16_t logicalPixelLimit_;
  bool calibrationActive_ = false;
  bool stripTestActive_ = false;
  uint8_t testedStripNumber_ = 0;
  uint16_t cursor_ = 0;
  uint16_t boundaries_[MAX_STRIPS - 1] = {};
  uint8_t boundaryCount_ = 0;
  ConfigValidationResult lastValidation_ = {
      ConfigValidationError::NONE, 0, 0};
};
