#include "PirSensor.h"

PirSensor::PirSensor(const uint8_t pin, const uint32_t debounceMs,
                     const uint32_t minRetriggerMs)
    : pin_(pin),
      debounceMs_(debounceMs),
      minRetriggerMs_(minRetriggerMs) {}

void PirSensor::begin(const uint32_t nowMs) {
  // AM312 already drives its output actively. Keep the ESP input high
  // impedance: an internal pull-down can load some module variants enough to
  // make their HIGH level marginal.
  pinMode(pin_, INPUT);
  rawHigh_ = digitalRead(pin_) == HIGH;
  stableHigh_ = rawHigh_;
  armed_ = !stableHigh_;
  hasTriggered_ = false;
  rawChangedAtMs_ = nowMs;
  lastTriggeredAtMs_ = nowMs;

  Serial.print("PIR GPIO");
  Serial.print(pin_);
  Serial.print(" initialized ");
  Serial.print(stableHigh_ ? "HIGH" : "LOW");
  Serial.println(armed_ ? " (armed)" : " (waiting for LOW)");
}

bool PirSensor::update(const uint32_t nowMs) {
  const bool isHigh = digitalRead(pin_) == HIGH;
  if (isHigh != rawHigh_) {
    rawHigh_ = isHigh;
    rawChangedAtMs_ = nowMs;
  }

  if (rawHigh_ == stableHigh_ ||
      nowMs - rawChangedAtMs_ < debounceMs_) {
    return false;
  }

  stableHigh_ = rawHigh_;
  Serial.print("PIR GPIO");
  Serial.print(pin_);
  Serial.print(" stable ");
  Serial.println(stableHigh_ ? "HIGH" : "LOW");

  if (!stableHigh_) {
    armed_ = true;
    return false;
  }

  if (!armed_) {
    return false;
  }

  armed_ = false;
  const bool retriggerAllowed =
      !hasTriggered_ || nowMs - lastTriggeredAtMs_ >= minRetriggerMs_;
  if (!retriggerAllowed) {
    return false;
  }

  hasTriggered_ = true;
  lastTriggeredAtMs_ = nowMs;
  return true;
}

bool PirSensor::isActive() const { return stableHigh_; }
