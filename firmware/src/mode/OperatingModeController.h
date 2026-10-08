#pragma once

#include <Arduino.h>

enum class OperatingMode : uint8_t {
  NORMAL,
  CONFIG_MODE,
};

enum class ConfigModeExitReason : uint8_t {
  NONE,
  SAVED,
  USER_REQUESTED,
  TIMEOUT,
};

class OperatingModeController {
 public:
  OperatingModeController(uint8_t buttonPin, uint32_t buttonHoldMs,
                          uint32_t configModeTimeoutMs);

  void begin(uint32_t nowMs);
  void update(uint32_t nowMs);

  bool isConfigMode() const;
  void enterConfigMode(uint32_t nowMs);
  void exitConfigMode(ConfigModeExitReason reason);
  void noteConfigActivity(uint32_t nowMs);

  bool shouldRestartNormal() const;
  bool isButtonReleased() const;
  uint32_t configModeRemainingMs(uint32_t nowMs) const;
  ConfigModeExitReason exitReason() const;

 private:
  const uint8_t buttonPin_;
  const uint32_t buttonHoldMs_;
  const uint32_t configModeTimeoutMs_;
  OperatingMode mode_ = OperatingMode::NORMAL;
  ConfigModeExitReason exitReason_ = ConfigModeExitReason::NONE;
  uint32_t buttonPressedAtMs_ = 0;
  uint32_t configModeStartedAtMs_ = 0;
  uint32_t lastConfigActivityAtMs_ = 0;
  bool buttonArmed_ = false;
  bool trackingButtonHold_ = false;
};
