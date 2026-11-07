#include "OutputDiagnostic.h"

#include <stdio.h>

namespace {
constexpr uint32_t CONSTANT_STAGE_MS = 4000;
// With the test's gamma=1 and brightness=80, raw 26 scales to output 8.
constexpr uint8_t LOW_RAW_LEVEL = 26;
}

OutputDiagnostic::OutputDiagnostic(LedController& leds)
    : leds_(leds), animation_(leds, config_) {}

void OutputDiagnostic::begin(const OutputDiagnosticStart& start) {
  config_ = animationDiagnosticConfig(start.config);
  diagnostics.reset({config_, "one-click-diagnostic", start.atMs});
  leds_.configure(config_.maxBrightness, config_.gamma);
  animation_.begin(start.atMs);
  stage_ = OutputDiagnosticStage::OFF;
  lowOutputLevel_ = 1;
  enterStage(OutputDiagnosticStage::HOLD_HIGH, start.atMs);
}

void OutputDiagnostic::update(const uint32_t nowMs) {
  if (!isActive()) return;
  if (stage_ == OutputDiagnosticStage::FADE) {
    animation_.update(nowMs, false);
    if (animation_.isOff()) stage_ = OutputDiagnosticStage::OFF;
    return;
  }

  const bool lowLevelStage = stage_ == OutputDiagnosticStage::LOW_HOLD ||
                             stage_ == OutputDiagnosticStage::LOW_REPEAT;
  const uint32_t durationMs = lowLevelStage ? 1000 : CONSTANT_STAGE_MS;
  if (nowMs - stageStartedAtMs_ >= durationMs) {
    switch (stage_) {
      case OutputDiagnosticStage::HOLD_HIGH:
        enterStage(OutputDiagnosticStage::REPEAT_HIGH, nowMs);
        break;
      case OutputDiagnosticStage::REPEAT_HIGH:
        enterStage(OutputDiagnosticStage::REPEAT_HIGH_50, nowMs);
        break;
      case OutputDiagnosticStage::REPEAT_HIGH_50:
        enterStage(OutputDiagnosticStage::HOLD_LOW, nowMs);
        return;  // enterStage has already sent the new low frame.
      case OutputDiagnosticStage::HOLD_LOW:
        enterStage(OutputDiagnosticStage::REPEAT_LOW, nowMs);
        break;
      case OutputDiagnosticStage::REPEAT_LOW:
        enterStage(OutputDiagnosticStage::REPEAT_LOW_50, nowMs);
        break;
      case OutputDiagnosticStage::REPEAT_LOW_50:
        enterStage(OutputDiagnosticStage::LOW_HOLD, nowMs);
        return;
      case OutputDiagnosticStage::LOW_HOLD:
        enterStage(OutputDiagnosticStage::LOW_REPEAT, nowMs);
        break;
      case OutputDiagnosticStage::LOW_REPEAT:
        if (lowOutputLevel_ < 7) {
          ++lowOutputLevel_;
          enterStage(OutputDiagnosticStage::LOW_HOLD, nowMs);
          return;
        }
        enterStage(OutputDiagnosticStage::FADE, nowMs);
        return;
      default:
        return;
    }
  }
  const bool repeat = stage_ == OutputDiagnosticStage::REPEAT_HIGH ||
                      stage_ == OutputDiagnosticStage::REPEAT_LOW ||
                      stage_ == OutputDiagnosticStage::REPEAT_HIGH_50 ||
                      stage_ == OutputDiagnosticStage::REPEAT_LOW_50 ||
                      stage_ == OutputDiagnosticStage::LOW_REPEAT;
  const bool limited = stage_ == OutputDiagnosticStage::REPEAT_HIGH_50 ||
                       stage_ == OutputDiagnosticStage::REPEAT_LOW_50;
  if (limited && nowMs - lastRepeatAtMs_ < 20) return;
  // Do not render or modify any pixel here. Re-send the identical buffer.
  if (repeat) {
    lastRepeatAtMs_ = nowMs;
    leds_.show();
  }
}

void OutputDiagnostic::stop(const uint32_t nowMs) {
  stage_ = OutputDiagnosticStage::OFF;
  animation_.begin(nowMs);
}

bool OutputDiagnostic::isActive() const {
  return stage_ != OutputDiagnosticStage::OFF;
}

OutputDiagnosticStage OutputDiagnostic::stage() const { return stage_; }

void OutputDiagnostic::enterStage(const OutputDiagnosticStage next,
                                  const uint32_t nowMs) {
  char previous[24];
  snprintf(previous, sizeof(previous), "%s", diagnostics.phase());
  diagnostics.transition({previous, stageName(next),
                          "output comparison stage", nowMs});
  stage_ = next;
  stageStartedAtMs_ = nowMs;
  lastRepeatAtMs_ = nowMs - 20;
  switch (next) {
    case OutputDiagnosticStage::HOLD_HIGH:
      showConstant(255);
      return;
    case OutputDiagnosticStage::HOLD_LOW:
      showConstant(LOW_RAW_LEVEL);
      return;
    case OutputDiagnosticStage::LOW_HOLD:
      // Exact output 1..7 at gamma=1 / brightness=80; no dithering.
      showConstant((lowOutputLevel_ * 256U + config_.maxBrightness) /
                   (config_.maxBrightness + 1U));
      return;
    case OutputDiagnosticStage::FADE:
      animation_.begin(nowMs);
      animation_.trigger(AnimationEngine::Direction::LEFT_TO_RIGHT, nowMs);
      return;
    default:
      return;
  }
}

void OutputDiagnostic::showConstant(const uint8_t level) {
  leds_.clear();
  leds_.setRange(0, configuredPixelCount(config_), level);
  leds_.show();
}

const char* OutputDiagnostic::stageName(const OutputDiagnosticStage stage) const {
  switch (stage) {
    case OutputDiagnosticStage::OFF: return "OFF";
    case OutputDiagnosticStage::HOLD_HIGH: return "HOLD_HIGH";
    case OutputDiagnosticStage::REPEAT_HIGH: return "REPEAT_HIGH";
    case OutputDiagnosticStage::REPEAT_HIGH_50: return "REPEAT_HIGH_50";
    case OutputDiagnosticStage::HOLD_LOW: return "HOLD_LOW";
    case OutputDiagnosticStage::REPEAT_LOW: return "REPEAT_LOW";
    case OutputDiagnosticStage::REPEAT_LOW_50: return "REPEAT_LOW_50";
    case OutputDiagnosticStage::LOW_HOLD: {
      constexpr const char* names[] = {"HOLD_1", "HOLD_2", "HOLD_3", "HOLD_4", "HOLD_5", "HOLD_6", "HOLD_7"};
      return names[lowOutputLevel_ - 1];
    }
    case OutputDiagnosticStage::LOW_REPEAT: {
      constexpr const char* names[] = {"REPEAT_1", "REPEAT_2", "REPEAT_3", "REPEAT_4", "REPEAT_5", "REPEAT_6", "REPEAT_7"};
      return names[lowOutputLevel_ - 1];
    }
    case OutputDiagnosticStage::FADE: return "FADE";
  }
  return "UNKNOWN";
}
