#include "Bh1750Sensor.h"

#include <Wire.h>

namespace {
constexpr uint8_t POWER_ON_COMMAND = 0x01;
constexpr uint8_t CONTINUOUS_HIGH_RESOLUTION_MODE = 0x10;
constexpr uint32_t FIRST_MEASUREMENT_DELAY_MS = 180;
constexpr float RAW_TO_LUX_DIVISOR = 1.2F;
}  // namespace

Bh1750Sensor::Bh1750Sensor(const uint8_t lowAddress,
                           const uint8_t highAddress,
                           const uint32_t readIntervalMs)
    : lowAddress_(lowAddress),
      highAddress_(highAddress),
      readIntervalMs_(readIntervalMs) {}

bool Bh1750Sensor::begin(const uint32_t nowMs) {
  address_ = probe(lowAddress_)
                 ? lowAddress_
                 : (probe(highAddress_) ? highAddress_ : 0);
  if (address_ == 0) {
    available_ = false;
    return false;
  }

  available_ = sendCommand(POWER_ON_COMMAND) &&
               sendCommand(CONTINUOUS_HIGH_RESOLUTION_MODE);
  measurementStartedAtMs_ = nowMs;
  readAttempted_ = false;
  return available_;
}

bool Bh1750Sensor::update(const uint32_t nowMs, float& lux) {
  if (!available_ ||
      nowMs - measurementStartedAtMs_ < FIRST_MEASUREMENT_DELAY_MS) {
    return false;
  }

  if (readAttempted_ && nowMs - lastReadAtMs_ < readIntervalMs_) {
    return false;
  }

  readAttempted_ = true;
  lastReadAtMs_ = nowMs;

  if (Wire.requestFrom(address_, static_cast<uint8_t>(2)) != 2) {
    return false;
  }

  const uint16_t rawValue =
      (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
  lux = rawValue / RAW_TO_LUX_DIVISOR;
  return true;
}

uint8_t Bh1750Sensor::address() const { return address_; }

bool Bh1750Sensor::probe(const uint8_t address) const {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool Bh1750Sensor::sendCommand(const uint8_t command) const {
  Wire.beginTransmission(address_);
  Wire.write(command);
  return Wire.endTransmission() == 0;
}
