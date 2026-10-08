#pragma once

#include <Arduino.h>

class PirSensor {
 public:
  PirSensor(uint8_t pin, uint32_t debounceMs, uint32_t minRetriggerMs);

  void begin(uint32_t nowMs);
  bool update(uint32_t nowMs);
  bool isActive() const;

 private:
  const uint8_t pin_;
  const uint32_t debounceMs_;
  const uint32_t minRetriggerMs_;
  bool rawHigh_ = false;
  bool stableHigh_ = false;
  bool armed_ = false;
  bool hasTriggered_ = false;
  uint32_t rawChangedAtMs_ = 0;
  uint32_t lastTriggeredAtMs_ = 0;
};
