#pragma once

#include <Arduino.h>

class PirSensor {
 public:
  explicit PirSensor(uint8_t pin);

  void begin();
  bool update();

 private:
  const uint8_t pin_;
  bool wasHigh_ = false;
};
