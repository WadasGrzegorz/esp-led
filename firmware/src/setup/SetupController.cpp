#include "SetupController.h"

namespace {
constexpr uint8_t COMPLETED_RANGE_LEVEL = 48;
constexpr uint8_t ACTIVE_RANGE_LEVEL = 112;
constexpr uint8_t CURSOR_LEVEL = 255;
constexpr uint8_t STRIP_TEST_LEVEL = 160;
}

SetupController::SetupController(LedController& leds,
                                 ConfigurationManager& configuration,
                                 const uint16_t logicalPixelLimit)
    : leds_(leds),
      configuration_(configuration),
      logicalPixelLimit_(logicalPixelLimit) {}

SetupResult SetupController::startCalibration() {
  if (calibrationActive_) {
    return SetupResult::ALREADY_ACTIVE;
  }

  calibrationActive_ = true;
  stripTestActive_ = false;
  cursor_ = 0;
  boundaryCount_ = 0;
  renderCalibration();
  return SetupResult::OK;
}

SetupResult SetupController::moveCursor(const int16_t delta) {
  if (!calibrationActive_) {
    return SetupResult::NOT_ACTIVE;
  }

  const int32_t target = static_cast<int32_t>(cursor_) + delta;
  if (target < currentStripStart() || target >= logicalPixelLimit_) {
    return SetupResult::OUT_OF_RANGE;
  }

  cursor_ = static_cast<uint16_t>(target);
  renderCalibration();
  return SetupResult::OK;
}

SetupResult SetupController::goToPixel(const uint16_t logicalPixel) {
  if (!calibrationActive_) {
    return SetupResult::NOT_ACTIVE;
  }
  if (logicalPixel < currentStripStart() ||
      logicalPixel >= logicalPixelLimit_) {
    return SetupResult::OUT_OF_RANGE;
  }

  cursor_ = logicalPixel;
  renderCalibration();
  return SetupResult::OK;
}

SetupResult SetupController::markBoundary() {
  if (!calibrationActive_) {
    return SetupResult::NOT_ACTIVE;
  }
  if (boundaryCount_ >= MAX_STRIPS - 1) {
    return SetupResult::TOO_MANY_STRIPS;
  }
  if (cursor_ >= logicalPixelLimit_ - 1) {
    return SetupResult::OUT_OF_RANGE;
  }

  boundaries_[boundaryCount_] = cursor_;
  ++boundaryCount_;
  ++cursor_;
  renderCalibration();
  return SetupResult::OK;
}

SetupResult SetupController::undoBoundary() {
  if (!calibrationActive_) {
    return SetupResult::NOT_ACTIVE;
  }
  if (boundaryCount_ == 0) {
    return SetupResult::NOTHING_TO_UNDO;
  }

  --boundaryCount_;
  cursor_ = boundaries_[boundaryCount_];
  renderCalibration();
  return SetupResult::OK;
}

SetupResult SetupController::finishCalibration() {
  if (!calibrationActive_) {
    return SetupResult::NOT_ACTIVE;
  }

  LightingConfig candidate = configuration_.get();
  candidate.configVersion = LIGHTING_CONFIG_VERSION;
  candidate.stripCount = boundaryCount_ + 1;

  uint16_t startPixel = 0;
  for (uint8_t stripIndex = 0; stripIndex < candidate.stripCount;
       ++stripIndex) {
    const uint16_t endPixel = stripIndex < boundaryCount_
                                  ? boundaries_[stripIndex]
                                  : cursor_;
    candidate.strips[stripIndex].pixelCount = endPixel - startPixel + 1;
    startPixel = endPixel + 1;
  }
  for (uint8_t stripIndex = candidate.stripCount; stripIndex < MAX_STRIPS;
       ++stripIndex) {
    candidate.strips[stripIndex] = {0, false};
  }

  lastValidation_ = configuration_.update(candidate);
  if (!lastValidation_.valid()) {
    return SetupResult::INVALID_CONFIG;
  }

  calibrationActive_ = false;
  leds_.clear();
  leds_.show();
  return SetupResult::OK;
}

SetupResult SetupController::cancelCalibration() {
  if (!calibrationActive_) {
    return SetupResult::NOT_ACTIVE;
  }

  calibrationActive_ = false;
  leds_.clear();
  leds_.show();
  return SetupResult::OK;
}

SetupResult SetupController::testStrip(const uint8_t stripNumber) {
  const LightingConfig& config = configuration_.get();
  if (stripNumber == 0 || stripNumber > config.stripCount) {
    return SetupResult::OUT_OF_RANGE;
  }

  calibrationActive_ = false;
  stripTestActive_ = true;
  testedStripNumber_ = stripNumber;
  renderStripTest();
  return SetupResult::OK;
}

SetupResult SetupController::stopStripTest() {
  if (!stripTestActive_) {
    return SetupResult::NOT_ACTIVE;
  }

  stripTestActive_ = false;
  leds_.clear();
  leds_.show();
  return SetupResult::OK;
}

void SetupController::refreshDisplay() {
  if (calibrationActive_) {
    renderCalibration();
    return;
  }
  if (stripTestActive_) {
    renderStripTest();
  }
}

bool SetupController::isCalibrationActive() const {
  return calibrationActive_;
}

bool SetupController::isStripTestActive() const { return stripTestActive_; }

bool SetupController::isServiceModeActive() const {
  return calibrationActive_ || stripTestActive_;
}

uint16_t SetupController::cursor() const { return cursor_; }

uint16_t SetupController::currentStripStart() const {
  return boundaryCount_ == 0 ? 0 : boundaries_[boundaryCount_ - 1] + 1;
}

uint8_t SetupController::markedBoundaryCount() const {
  return boundaryCount_;
}

uint16_t SetupController::markedBoundary(const uint8_t index) const {
  return index < boundaryCount_ ? boundaries_[index] : 0;
}

ConfigValidationResult SetupController::lastValidation() const {
  return lastValidation_;
}

void SetupController::renderCalibration() {
  leds_.clear();

  uint16_t startPixel = 0;
  for (uint8_t boundaryIndex = 0; boundaryIndex < boundaryCount_;
       ++boundaryIndex) {
    const uint16_t endPixel = boundaries_[boundaryIndex];
    leds_.setRange(startPixel, endPixel - startPixel + 1,
                   COMPLETED_RANGE_LEVEL);
    startPixel = endPixel + 1;
  }

  leds_.setRange(startPixel, cursor_ - startPixel + 1, ACTIVE_RANGE_LEVEL);
  leds_.setLogicalPixel(cursor_, CURSOR_LEVEL);
  leds_.show();
}

void SetupController::renderStripTest() {
  const LightingConfig& config = configuration_.get();
  if (testedStripNumber_ == 0 || testedStripNumber_ > config.stripCount) {
    stopStripTest();
    return;
  }

  const uint8_t stripIndex = testedStripNumber_ - 1;
  const uint16_t startPixel = stripStartPixel(stripIndex);
  leds_.clear();
  leds_.setRange(startPixel, config.strips[stripIndex].pixelCount,
                 STRIP_TEST_LEVEL);
  leds_.show();
}

uint16_t SetupController::stripStartPixel(const uint8_t stripIndex) const {
  const LightingConfig& config = configuration_.get();
  uint16_t startPixel = 0;
  for (uint8_t index = 0; index < stripIndex; ++index) {
    startPixel += config.strips[index].pixelCount;
  }
  return startPixel;
}
