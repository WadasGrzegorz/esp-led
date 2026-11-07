#pragma once

#include "Diagnostics.h"
#include "../led/AnimationEngine.h"

// A finite service test, separate from the normal animation and NVS config.
class OutputDiagnostic {
 public:
  explicit OutputDiagnostic(LedController& leds);
  void begin(const OutputDiagnosticStart& start);
  void update(uint32_t nowMs);
  void stop(uint32_t nowMs);
  bool isActive() const;
  OutputDiagnosticStage stage() const;

 private:
  void enterStage(OutputDiagnosticStage next, uint32_t nowMs);
  void showConstant(uint8_t level);
  const char* stageName(OutputDiagnosticStage stage) const;

  LedController& leds_;
  LightingConfig config_ = {};
  AnimationEngine animation_;
  OutputDiagnosticStage stage_ = OutputDiagnosticStage::OFF;
  uint32_t stageStartedAtMs_ = 0;
  uint32_t lastRepeatAtMs_ = 0;
  uint8_t lowOutputLevel_ = 1;
};
