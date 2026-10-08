#include "OperatingModeController.h"

OperatingModeController::OperatingModeController(
    const uint8_t buttonPin, const uint32_t buttonHoldMs,
    const uint32_t configModeTimeoutMs)
    : buttonPin_(buttonPin),
      buttonHoldMs_(buttonHoldMs),
      configModeTimeoutMs_(configModeTimeoutMs) {}

void OperatingModeController::begin(const uint32_t nowMs) {
  pinMode(buttonPin_, INPUT_PULLUP);
  mode_ = OperatingMode::NORMAL;
  exitReason_ = ConfigModeExitReason::NONE;
  buttonPressedAtMs_ = nowMs;
  configModeStartedAtMs_ = nowMs;
  trackingButtonHold_ = false;
  buttonArmed_ = digitalRead(buttonPin_) == HIGH;

  Serial.println("Normal boot");
  Serial.print("CONFIG MODE locked; hold BOOT for ");
  Serial.print(buttonHoldMs_ / 1000UL);
  Serial.println(" seconds after startup to enter");
}

void OperatingModeController::update(const uint32_t nowMs) {
  if (isConfigMode()) {
    if (configModeRemainingMs(nowMs) > 0) {
      return;
    }

    Serial.println("CONFIG MODE timeout expired -> restarting NORMAL");
    exitConfigMode(ConfigModeExitReason::TIMEOUT);
    return;
  }

  if (shouldRestartNormal()) {
    return;
  }

  const bool buttonPressed = digitalRead(buttonPin_) == LOW;
  if (!buttonArmed_) {
    buttonArmed_ = !buttonPressed;
    return;
  }

  if (!buttonPressed) {
    trackingButtonHold_ = false;
    return;
  }

  if (!trackingButtonHold_) {
    trackingButtonHold_ = true;
    buttonPressedAtMs_ = nowMs;
    return;
  }

  if (nowMs - buttonPressedAtMs_ >= buttonHoldMs_) {
    enterConfigMode(nowMs);
  }
}

bool OperatingModeController::isConfigMode() const {
  return mode_ == OperatingMode::CONFIG_MODE;
}

void OperatingModeController::enterConfigMode(const uint32_t nowMs) {
  if (isConfigMode() || shouldRestartNormal()) {
    return;
  }

  mode_ = OperatingMode::CONFIG_MODE;
  configModeStartedAtMs_ = nowMs;
  lastConfigActivityAtMs_ = nowMs;
  trackingButtonHold_ = false;
  Serial.println("BOOT hold detected / entering CONFIG MODE");
  Serial.print("CONFIG MODE active; timeout in ");
  Serial.print(configModeTimeoutMs_ / 1000UL);
  Serial.println(" seconds");
}

void OperatingModeController::noteConfigActivity(const uint32_t nowMs) {
  if (isConfigMode()) {
    lastConfigActivityAtMs_ = nowMs;
  }
}

void OperatingModeController::exitConfigMode(
    const ConfigModeExitReason reason) {
  if (!isConfigMode()) {
    return;
  }

  mode_ = OperatingMode::NORMAL;
  exitReason_ = reason;
  trackingButtonHold_ = false;
}

bool OperatingModeController::shouldRestartNormal() const {
  return exitReason_ != ConfigModeExitReason::NONE;
}

bool OperatingModeController::isButtonReleased() const {
  return digitalRead(buttonPin_) == HIGH;
}

uint32_t OperatingModeController::configModeRemainingMs(
    const uint32_t nowMs) const {
  if (!isConfigMode()) {
    return 0;
  }

  const uint32_t idleElapsedMs = nowMs - lastConfigActivityAtMs_;
  const uint32_t sessionElapsedMs = nowMs - configModeStartedAtMs_;
  const uint32_t maximumSessionMs = configModeTimeoutMs_ * 3UL;
  const uint32_t idleRemainingMs = idleElapsedMs >= configModeTimeoutMs_
                                       ? 0
                                       : configModeTimeoutMs_ - idleElapsedMs;
  const uint32_t sessionRemainingMs = sessionElapsedMs >= maximumSessionMs
                                          ? 0
                                          : maximumSessionMs - sessionElapsedMs;
  return min(idleRemainingMs, sessionRemainingMs);
}

ConfigModeExitReason OperatingModeController::exitReason() const {
  return exitReason_;
}
