#include "PirSensor.h"

PirSensor::PirSensor(const uint8_t pin) : pin_(pin) {}

void PirSensor::begin() {
  pinMode(pin_, INPUT);
  wasHigh_ = digitalRead(pin_) == HIGH;
}

bool PirSensor::update() {
  const bool isHigh = digitalRead(pin_) == HIGH;
  const bool triggered = isHigh && !wasHigh_;
  wasHigh_ = isHigh;
  return triggered;
}
